use worker::*;
use std::sync::Mutex;
use std::collections::HashMap;
use crate::errors::{AppError, Result};
use crate::config::RelayConfig;
use crate::auth::models::*;
use crate::auth::passwords::{hash_password, verify_password};
use crate::auth::tokens::{
    generate_access_token_string, generate_refresh_token_string, hash_token_str,
};
use crate::auth::middleware::get_auth_context;
use crate::db::*;
use hmac::{Hmac, Mac};
use sha2::{Sha256, Digest};

const ACCESS_TOKEN_TTL_SECS: i64 = 60 * 60;
const REFRESH_TOKEN_TTL_SECS: i64 = 60 * 60 * 24 * 30;
const LOGIN_LOCK_THRESHOLD: i32 = 10;
const LOGIN_LOCK_SECS: i64 = 15 * 60;

fn check_rate_limit(ip: &str, limit_per_min: u32, now: i64) -> bool {
    static LIMITER: std::sync::OnceLock<Mutex<HashMap<String, Vec<i64>>>> = std::sync::OnceLock::new();
    let mut map = LIMITER.get_or_init(|| Mutex::new(HashMap::new())).lock().unwrap();
    let timestamps = map.entry(ip.to_string()).or_default();
    
    timestamps.retain(|&t| t > now - 60);
    
    if timestamps.len() >= limit_per_min as usize {
        false
    } else {
        timestamps.push(now);
        true
    }
}

fn js<T: serde::Serialize>(val: &T) -> std::result::Result<wasm_bindgen::JsValue, AppError> {
    let js_val = serde_wasm_bindgen::to_value(val).map_err(|e| AppError::Internal(e.to_string()))?;
    if js_val.is_undefined() {
        Ok(wasm_bindgen::JsValue::null())
    } else {
        Ok(js_val)
    }
}

pub async fn signup(mut req: Request, env: Env) -> Result<Response> {
    let payload = req.json::<SignupRequest>().await?;
    let ip = req.headers().get("cf-connecting-ip")?.unwrap_or_else(|| "127.0.0.1".to_string());
    let now = chrono::Utc::now().timestamp();
    let config = RelayConfig::from_env(&env);
    
    if !check_rate_limit(&ip, config.signup_rate_limit_per_ip_per_minute, now) {
        return Err(AppError::RateLimit("signup rate limit exceeded".to_string()));
    }
    if !config.open_registration {
        return Err(AppError::BadRequest("registration is disabled".to_string()));
    }
    
    let email = normalize_email(&payload.email)
        .ok_or_else(|| AppError::BadRequest("email is required".to_string()))?;
    if payload.password.trim().is_empty() {
        return Err(AppError::BadRequest("password is required".to_string()));
    }
    if payload.password.chars().count() < config.min_password_length {
        return Err(AppError::BadRequest(format!(
            "password must be at least {} characters",
            config.min_password_length
        )));
    }
    
    let db = env.d1("MAHO_DB")?;
    if find_user_by_email(&db, &email).await?.is_some() {
        return Err(AppError::Conflict("email already exists".to_string()));
    }
    
    let pepper = env.secret("ARGON2_PEPPER")?.to_string();
    let password_hash = hash_password(&payload.password, pepper.as_bytes())?;
    
    let display_name = normalize_display_name(payload.display_name.as_deref(), &email);
    let device_name = normalize_device_name(payload.device_name.as_deref());
    let device_type = normalize_device_type(payload.device_type.as_deref());
    let room_id = uuid::Uuid::new_v4().simple().to_string();
    let access_token = generate_access_token_string();
    let refresh_token = generate_refresh_token_string();
    
    let user = insert_user(&db, &email, &password_hash, &display_name, now).await?;
    let device = insert_device(&db, user.id, &device_name, &device_type, &room_id, now).await?;
    
    let access_hash = hash_token_str(&access_token).to_vec();
    let refresh_hash = hash_token_str(&refresh_token).to_vec();
    
    let session = insert_session(
        &db,
        user.id,
        device.id,
        &access_hash,
        &refresh_hash,
        now + ACCESS_TOKEN_TTL_SECS,
        now + REFRESH_TOKEN_TTL_SECS,
        now,
    ).await?;
    
    let auth_res = AuthResponse {
        access_token,
        refresh_token,
        token_type: "Bearer",
        expires_at: session.expires_at,
        refresh_expires_at: session.refresh_expires_at,
        account: PublicUser::from(user),
        device: PublicDevice::from(device),
        session: PublicSession::from(session),
    };
    
    Ok(Response::from_json(&auth_res)?)
}

pub async fn login(mut req: Request, env: Env) -> Result<Response> {
    let payload = req.json::<LoginRequest>().await?;
    let ip = req.headers().get("cf-connecting-ip")?.unwrap_or_else(|| "127.0.0.1".to_string());
    let now = chrono::Utc::now().timestamp();
    let config = RelayConfig::from_env(&env);
    
    if !check_rate_limit(&ip, config.login_rate_limit_per_ip_per_minute, now) {
        return Err(AppError::RateLimit("login rate limit exceeded".to_string()));
    }
    
    let email = normalize_email(&payload.email)
        .ok_or_else(|| AppError::BadRequest("email is required".to_string()))?;
    if payload.password.trim().is_empty() {
        return Err(AppError::BadRequest("password is required".to_string()));
    }
    
    let db = env.d1("MAHO_DB")?;
    let mut user = find_user_by_email(&db, &email).await?
        .ok_or_else(|| AppError::Auth("invalid credentials".to_string()))?;
        
    if user.locked_until.map(|locked_until| locked_until > now).unwrap_or(false) {
        return Err(AppError::Auth("account is temporarily locked".to_string()));
    }
    
    let pepper = env.secret("ARGON2_PEPPER")?.to_string();
    let password_ok = verify_password(&payload.password, &user.password_hash, pepper.as_bytes())?;
    
    if !password_ok {
        user.failed_login_attempts += 1;
        user.last_failed_login_at = Some(now);
        user.locked_until = if user.failed_login_attempts >= LOGIN_LOCK_THRESHOLD {
            Some(now + LOGIN_LOCK_SECS)
        } else {
            None
        };
        update_user_login_state(&db, user.id, user.failed_login_attempts, user.locked_until, user.last_failed_login_at).await?;
        
        return Err(AppError::Auth("invalid credentials".to_string()));
    }
    
    user.failed_login_attempts = 0;
    user.last_failed_login_at = None;
    user.locked_until = None;
    update_user_login_state(&db, user.id, 0, None, None).await?;
    
    let device_name = normalize_device_name(payload.device_name.as_deref());
    let device_type = normalize_device_type(payload.device_type.as_deref());
    let room_id = uuid::Uuid::new_v4().simple().to_string();
    let access_token = generate_access_token_string();
    let refresh_token = generate_refresh_token_string();
    
    let device = insert_device(&db, user.id, &device_name, &device_type, &room_id, now).await?;
    
    let access_hash = hash_token_str(&access_token).to_vec();
    let refresh_hash = hash_token_str(&refresh_token).to_vec();
    
    let session = insert_session(
        &db,
        user.id,
        device.id,
        &access_hash,
        &refresh_hash,
        now + ACCESS_TOKEN_TTL_SECS,
        now + REFRESH_TOKEN_TTL_SECS,
        now,
    ).await?;
    
    let auth_res = AuthResponse {
        access_token,
        refresh_token,
        token_type: "Bearer",
        expires_at: session.expires_at,
        refresh_expires_at: session.refresh_expires_at,
        account: PublicUser::from(user),
        device: PublicDevice::from(device),
        session: PublicSession::from(session),
    };
    
    Ok(Response::from_json(&auth_res)?)
}

pub async fn logout(req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let auth = get_auth_context(&req, &db).await?;
    
    delete_session_by_id(&db, auth.session.id).await?;
    Ok(Response::from_json(&LogoutResponse { ok: true })?)
}

pub async fn refresh(mut req: Request, env: Env) -> Result<Response> {
    let payload = req.json::<RefreshRequest>().await?;
    let db = env.d1("MAHO_DB")?;
    let now = chrono::Utc::now().timestamp();
    
    if payload.refresh_token.trim().is_empty() {
        return Err(AppError::BadRequest("refresh_token is required".to_string()));
    }
    
    let refresh_hash = hash_token_str(&payload.refresh_token);
    let session = find_session_by_refresh_hash(&db, &refresh_hash).await?
        .ok_or_else(|| AppError::Auth("invalid refresh token".to_string()))?;
        
    if session.refresh_expires_at <= now {
        return Err(AppError::Auth("refresh token expired".to_string()));
    }
    
    let user = find_user_by_id(&db, session.user_id).await?
        .ok_or_else(|| AppError::Internal("user missing".to_string()))?;
    let device = find_device_by_id(&db, session.device_id).await?
        .ok_or_else(|| AppError::Internal("device missing".to_string()))?;
        
    let access_token = generate_access_token_string();
    let refresh_token = generate_refresh_token_string();
    
    let access_hash = hash_token_str(&access_token).to_vec();
    let refresh_hash = hash_token_str(&refresh_token).to_vec();
    
    let updated = rotate_session_tokens(
        &db,
        session.id,
        &access_hash,
        &refresh_hash,
        now + ACCESS_TOKEN_TTL_SECS,
        now + REFRESH_TOKEN_TTL_SECS,
        now,
    ).await?;
    
    touch_device(&db, device.id, now).await?;
    
    let auth_res = AuthResponse {
        access_token,
        refresh_token,
        token_type: "Bearer",
        expires_at: updated.expires_at,
        refresh_expires_at: updated.refresh_expires_at,
        account: PublicUser::from(user),
        device: PublicDevice::from(device),
        session: PublicSession::from(updated),
    };
    
    Ok(Response::from_json(&auth_res)?)
}

pub async fn me(req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let auth = get_auth_context(&req, &db).await?;
    
    let devices = list_devices_for_user(&db, auth.user.id).await?
        .into_iter()
        .map(PublicDevice::from)
        .collect();
        
    let me_res = MeResponse {
        account: PublicUser::from(auth.user),
        devices,
        current_device: PublicDevice::from(auth.device),
        current_session: PublicSession::from(auth.session),
    };
    
    Ok(Response::from_json(&me_res)?)
}

fn tier_ceiling_usd(tier: &str) -> f64 {
    match tier {
        "pro" => 2.10,
        "max" => 13.30,
        _ => 0.50,
    }
}

pub async fn subscription(req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let auth = get_auth_context(&req, &db).await?;
    
    // Get credit balance (raw SQL)
    let stmt = db.prepare("SELECT balance_usd, lifetime_purchased_usd FROM credits WHERE user_id = ?1");
    let stmt = stmt.bind(&[js(&auth.user.id)?])?;
    let credits_row = stmt.first::<(f64, f64)>(None).await?
        .unwrap_or((0.0, 0.0));
        
    let sub_res = SubscriptionResponse {
        tier_ceiling_usd: tier_ceiling_usd(&auth.user.tier),
        tier: auth.user.tier,
        subscription_status: auth.user.subscription_status,
        subscription_expires_at: auth.user.subscription_expires_at,
        period_start_at: auth.user.period_start_at,
        anniversary_reset_day: auth.user.anniversary_reset_day,
        credit_balance_usd: credits_row.0,
        lifetime_purchased_usd: credits_row.1,
        has_payment_method: auth.user.has_payment_method != 0.0,
    };
    
    Ok(Response::from_json(&sub_res)?)
}

pub async fn delete_device(req: Request, env: Env, device_id_str: &str) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let auth = get_auth_context(&req, &db).await?;
    
    let device_id: i32 = device_id_str.parse()
        .map_err(|_| AppError::BadRequest("invalid device id".to_string()))?;
        
    let device = find_device_by_id(&db, device_id).await?
        .ok_or_else(|| AppError::NotFound("device not found".to_string()))?;
        
    if device.user_id != auth.user.id {
        return Err(AppError::Auth("device does not belong to current user".to_string()));
    }
    
    delete_device_and_sessions(&db, device.id).await?;
    Ok(Response::from_json(&DeleteDeviceResponse { ok: true })?)
}

pub async fn introspect(mut req: Request, env: Env) -> Result<Response> {
    let payload = req.json::<IntrospectRequest>().await?;
    
    let auth_header = req.headers().get("Authorization")?
        .ok_or_else(|| AppError::Auth("missing authorization header".to_string()))?;
    let service_token = auth_header
        .strip_prefix("Bearer ")
        .or_else(|| auth_header.strip_prefix("bearer "))
        .map(str::trim)
        .ok_or_else(|| AppError::Auth("invalid service token format".to_string()))?;
        
    let expected_service_token = env.var("MAHO_RELAY_SERVICE_TOKEN")
        .map(|v| v.to_string())
        .unwrap_or_default();
        
    if service_token != expected_service_token {
        return Err(AppError::Auth("invalid service token".to_string()));
    }
    
    let db = env.d1("MAHO_DB")?;
    let token_hash = hash_token_str(&payload.token);
    let context = find_active_session_context_by_access_hash(&db, &token_hash).await?
        .ok_or_else(|| AppError::Auth("invalid or expired user token".to_string()))?;
        
    let introspect_res = IntrospectResponse {
        user_id: context.user.id,
        tier: context.user.tier,
        subscription_status: context.user.subscription_status,
        email_verified: context.user.email_verified != 0.0,
        expires_at: context.session.expires_at,
        period_start_at: context.user.period_start_at,
    };
    
    Ok(Response::from_json(&introspect_res)?)
}

pub async fn request_verify(req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let auth = get_auth_context(&req, &db).await?;
    
    let token = uuid::Uuid::new_v4().simple().to_string();
    let expires_at = chrono::Utc::now().timestamp() + 24 * 3600; // 24 hours
    
    let hashed = format!("{:x}", sha2::Sha256::digest(token.as_bytes()));
    let stmt = db.prepare("UPDATE users SET verification_token = ?1, verification_token_expires_at = ?2 WHERE id = ?3");
    let stmt = stmt.bind(&[js(&hashed)?, js(&expires_at)?, js(&auth.user.id)?])?;
    stmt.run().await?;
    
    let dev_token = Some(token);
    
    Ok(Response::from_json(&RequestVerifyResponse {
        status: "success".to_string(),
        dev_token,
    })?)
}

pub async fn verify(mut req: Request, env: Env) -> Result<Response> {
    let payload = req.json::<VerifyRequest>().await?;
    let now = chrono::Utc::now().timestamp();
    let db = env.d1("MAHO_DB")?;
    
    let hashed = format!("{:x}", sha2::Sha256::digest(payload.token.as_bytes()));
    let stmt = db.prepare("SELECT * FROM users WHERE verification_token = ?1 AND verification_token_expires_at > ?2");
    let stmt = stmt.bind(&[js(&hashed)?, js(&now)?])?;
    let user = stmt.first::<User>(None).await?
        .ok_or_else(|| AppError::BadRequest("invalid or expired verification token".to_string()))?;
        
    let stmt2 = db.prepare("UPDATE users SET email_verified = 1, verification_token = NULL, verification_token_expires_at = NULL WHERE id = ?1");
    let stmt2 = stmt2.bind(&[js(&user.id)?])?;
    stmt2.run().await?;
    
    let service_token = env.var("MAHO_RELAY_SERVICE_TOKEN")
        .map(|v| v.to_string())
        .unwrap_or_default();
        
    let bypass_token = generate_bypass_token(user.id, &service_token);
    
    Ok(Response::from_json(&VerifyResponse {
        status: "success".to_string(),
        verify_bypass_token: bypass_token,
    })?)
}

pub async fn consume_bypass(mut req: Request, env: Env) -> Result<Response> {
    let payload = req.json::<ConsumeBypassRequest>().await?;
    
    let auth_header = req.headers().get("Authorization")?
        .ok_or_else(|| AppError::Auth("missing authorization header".to_string()))?;
    let service_token = auth_header
        .strip_prefix("Bearer ")
        .or_else(|| auth_header.strip_prefix("bearer "))
        .map(str::trim)
        .ok_or_else(|| AppError::Auth("invalid service token format".to_string()))?;
        
    let expected_service_token = env.var("MAHO_RELAY_SERVICE_TOKEN")
        .map(|v| v.to_string())
        .unwrap_or_default();
        
    if service_token != expected_service_token {
        return Err(AppError::Auth("invalid service token".to_string()));
    }
    
    let (user_id, _expires_at) = parse_bypass_token(&payload.token, &expected_service_token)
        .ok_or_else(|| AppError::Auth("invalid or expired bypass token".to_string()))?;
        
    let db = env.d1("MAHO_DB")?;
    let token_hash = sha2::Sha256::digest(payload.token.as_bytes()).to_vec();
    let now = chrono::Utc::now().timestamp();
    
    let stmt_check = db.prepare("SELECT user_id FROM consumed_bypass_tokens WHERE token_hash = ?1");
    let stmt_check = stmt_check.bind(&[js(&token_hash)?])?;
    let already_consumed = stmt_check.first::<i32>(Some("user_id")).await?.is_some();
    
    if already_consumed {
        return Err(AppError::BadRequest("bypass token already consumed".to_string()));
    }
    
    let stmt_insert = db.prepare("INSERT INTO consumed_bypass_tokens (token_hash, user_id, consumed_at) VALUES (?1, ?2, ?3)");
    let stmt_insert = stmt_insert.bind(&[js(&token_hash)?, js(&user_id)?, js(&now)?])?;
    stmt_insert.run().await?;
    
    Ok(Response::from_json(&ConsumeBypassResponse { user_id })?)
}

pub async fn admin_force_verify_email(mut req: Request, env: Env) -> Result<Response> {
    let payload = req.json::<AdminForceVerifyEmailRequest>().await?;
    
    let auth_header = req.headers().get("Authorization")?
        .ok_or_else(|| AppError::Auth("missing authorization header".to_string()))?;
    let service_token = auth_header
        .strip_prefix("Bearer ")
        .or_else(|| auth_header.strip_prefix("bearer "))
        .map(str::trim)
        .ok_or_else(|| AppError::Auth("invalid service token format".to_string()))?;
        
    let expected_service_token = env.var("MAHO_RELAY_SERVICE_TOKEN")
        .map(|v| v.to_string())
        .unwrap_or_default();
        
    if service_token != expected_service_token {
        return Err(AppError::Auth("invalid service token".to_string()));
    }
    
    let db = env.d1("MAHO_DB")?;
    let exists = find_user_by_id(&db, payload.user_id).await?.is_some();
    if !exists {
        return Err(AppError::NotFound("user not found".to_string()));
    }
    
    let stmt = db.prepare("UPDATE users SET email_verified = 1 WHERE id = ?1");
    let stmt = stmt.bind(&[js(&payload.user_id)?])?;
    stmt.run().await?;
    
    Ok(Response::from_json(&AdminForceVerifyEmailResponse {
        user_id: payload.user_id,
        email_verified: true,
    })?)
}

pub async fn admin_set_credit_balance(mut req: Request, env: Env) -> Result<Response> {
    let payload = req.json::<AdminSetCreditBalanceRequest>().await?;
    
    let auth_header = req.headers().get("Authorization")?
        .ok_or_else(|| AppError::Auth("missing authorization header".to_string()))?;
    let service_token = auth_header
        .strip_prefix("Bearer ")
        .or_else(|| auth_header.strip_prefix("bearer "))
        .map(str::trim)
        .ok_or_else(|| AppError::Auth("invalid service token format".to_string()))?;
        
    let expected_service_token = env.var("MAHO_RELAY_SERVICE_TOKEN")
        .map(|v| v.to_string())
        .unwrap_or_default();
        
    if service_token != expected_service_token {
        return Err(AppError::Auth("invalid service token".to_string()));
    }
    
    let db = env.d1("MAHO_DB")?;
    let now = chrono::Utc::now().timestamp();
    
    let stmt_check = db.prepare("SELECT user_id FROM credits WHERE user_id = ?1");
    let stmt_check = stmt_check.bind(&[js(&payload.user_id)?])?;
    let exists = stmt_check.first::<i32>(Some("user_id")).await?.is_some();
    
    if exists {
        let stmt_up = db.prepare("UPDATE credits SET balance_usd = ?1, updated_at = ?2 WHERE user_id = ?3");
        let stmt_up = stmt_up.bind(&[js(&payload.new_balance_usd)?, js(&now)?, js(&payload.user_id)?])?;
        stmt_up.run().await?;
    } else {
        let stmt_in = db.prepare("INSERT INTO credits (user_id, balance_usd, lifetime_purchased_usd, created_at, updated_at) VALUES (?1, ?2, 0.0, ?3, ?3)");
        let stmt_in = stmt_in.bind(&[js(&payload.user_id)?, js(&payload.new_balance_usd)?, js(&now)?])?;
        stmt_in.run().await?;
    }
    
    Ok(Response::from_json(&AdminSetCreditBalanceResponse {
        user_id: payload.user_id,
        new_balance_usd: payload.new_balance_usd,
    })?)
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
        if sig_hex.len() % 2 != 0 {
            return None;
        }
        let mut bytes = Vec::with_capacity(sig_hex.len() / 2);
        for i in (0..sig_hex.len()).step_by(2) {
            let b = u8::from_str_radix(&sig_hex[i..i+2], 16).ok()?;
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
    let mut mac = Hmac::<Sha256>::new_from_slice(service_token.as_bytes()).ok()?;
    mac.update(payload.as_bytes());
    let computed = mac.finalize().into_bytes();
    
    use subtle::ConstantTimeEq;
    if computed.as_slice().ct_eq(&sig_bytes).unwrap_u8() != 1 {
        return None;
    }
    
    let user_id: i32 = user_id_str.parse().ok()?;
    Some((user_id, expires_at))
}

fn generate_bypass_token(user_id: i32, service_token: &str) -> String {
    let expires_at = chrono::Utc::now().timestamp() + 300; // 5 minutes
    let payload = format!("{}:{}", user_id, expires_at);
    let mut mac = Hmac::<Sha256>::new_from_slice(service_token.as_bytes()).unwrap();
    mac.update(payload.as_bytes());
    let sig = format!("{:x}", mac.finalize().into_bytes());
    format!("{}:{}:{}", user_id, expires_at, sig)
}

impl From<User> for PublicUser {
    fn from(value: User) -> Self {
        Self {
            id: value.id,
            email: value.email,
            display_name: value.display_name,
            created_at: value.created_at,
            locked_until: value.locked_until,
            failed_login_attempts: value.failed_login_attempts,
            last_failed_login_at: value.last_failed_login_at,
        }
    }
}

impl From<Device> for PublicDevice {
    fn from(value: Device) -> Self {
        Self {
            id: value.id,
            user_id: value.user_id,
            name: value.name,
            device_type: value.device_type,
            room_id: value.room_id,
            created_at: value.created_at,
            last_seen: value.last_seen,
        }
    }
}

impl From<Session> for PublicSession {
    fn from(value: Session) -> Self {
        Self {
            id: value.id,
            user_id: value.user_id,
            device_id: value.device_id,
            expires_at: value.expires_at,
            refresh_expires_at: value.refresh_expires_at,
            last_used_at: value.last_used_at,
            created_at: value.created_at,
        }
    }
}
