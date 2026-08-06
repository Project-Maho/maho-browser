use crate::auth::google;
use crate::auth::middleware::get_auth_context;
use crate::auth::models::*;
use crate::auth::passwords::{hash_password, verify_password};
use crate::auth::tokens::{
    generate_access_token_string, generate_refresh_token_string, hash_token_str,
};
use crate::config::RelayConfig;
use crate::db::*;
use crate::errors::{AppError, Result};
use hmac::{Hmac, Mac};
use serde::Serialize;
use sha2::{Digest, Sha256};
use worker::*;

const ACCESS_TOKEN_TTL_SECS: i64 = 60 * 60;
const REFRESH_TOKEN_TTL_SECS: i64 = 60 * 60 * 24 * 30;
const LOGIN_LOCK_THRESHOLD: i32 = 10;
const LOGIN_LOCK_SECS: i64 = 15 * 60;
const GOOGLE_PROVIDER: &str = "google";

/// Constant-time service-token comparison to avoid a timing side-channel.
/// A `!=` on `&str` short-circuits at the first differing byte, leaking how
/// many leading bytes were guessed correctly. Token length is not secret, so
/// an early length-mismatch is acceptable.
pub(crate) fn service_token_matches(provided: &str, expected: &str) -> bool {
    use subtle::ConstantTimeEq;
    if expected.trim().is_empty() || provided.trim().is_empty() {
        return false;
    }
    provided.as_bytes().ct_eq(expected.as_bytes()).unwrap_u8() == 1
}

pub(crate) fn extract_bearer(req: &Request) -> Result<String> {
    let auth_header = req
        .headers()
        .get("Authorization")?
        .ok_or_else(|| AppError::Auth("missing authorization header".to_string()))?;
    auth_header
        .strip_prefix("Bearer ")
        .or_else(|| auth_header.strip_prefix("bearer "))
        .map(|s| s.trim().to_string())
        .ok_or_else(|| AppError::Auth("invalid token format".to_string()))
}

pub(crate) fn require_service_token(req: &Request, env: &Env) -> Result<()> {
    let token = extract_bearer(req)?;
    let expected = env
        .secret("MAHO_RELAY_SERVICE_TOKEN")
        .map(|v| v.to_string())
        .unwrap_or_default();
    if !service_token_matches(&token, &expected) {
        return Err(AppError::Auth("invalid service token".to_string()));
    }
    Ok(())
}

pub(crate) fn require_admin_token(req: &Request, env: &Env) -> Result<()> {
    let token = extract_bearer(req)?;
    let expected = env
        .secret("MAHO_ADMIN_TOKEN")
        .map(|v| v.to_string())
        .unwrap_or_default();
    if expected.trim().is_empty() {
        return Err(AppError::Internal(
            "MAHO_ADMIN_TOKEN not configured".to_string(),
        ));
    }
    if !service_token_matches(&token, &expected) {
        return Err(AppError::Auth("invalid admin token".to_string()));
    }
    Ok(())
}

async fn resolve_device(
    db: &D1Database,
    user_id: i32,
    client_device_id: Option<&str>,
    device_name: &str,
    device_type: &str,
    now: i64,
) -> Result<Device> {
    if let Some(cid) = client_device_id {
        if !is_valid_client_device_id(cid) {
            return Err(AppError::BadRequest("invalid device_id".to_string()));
        }
        if let Some(device) = find_device_by_client_id(db, user_id, cid).await? {
            touch_device(db, device.id, now).await?;
            return Ok(device);
        }
        let room_id = uuid::Uuid::new_v4().simple().to_string();
        if let Some(device) = insert_device(
            db,
            user_id,
            device_name,
            device_type,
            &room_id,
            Some(cid),
            now,
        )
        .await?
        {
            return Ok(device);
        }
        // Lost a concurrent first-login race (partial unique index on
        // (user_id, client_device_id)); the other login inserted it. Reuse it.
        return find_device_by_client_id(db, user_id, cid)
            .await?
            .ok_or_else(|| {
                AppError::Internal("device vanished after insert conflict".to_string())
            });
    }
    let room_id = uuid::Uuid::new_v4().simple().to_string();
    insert_device(db, user_id, device_name, device_type, &room_id, None, now)
        .await?
        .ok_or_else(|| AppError::Internal("device insert returned no row".to_string()))
}

pub async fn signup(mut req: Request, env: Env) -> Result<Response> {
    let payload = req.json::<SignupRequest>().await?;
    let ip = req
        .headers()
        .get("cf-connecting-ip")?
        .unwrap_or_else(|| "127.0.0.1".to_string());
    let now = chrono::Utc::now().timestamp();
    let config = RelayConfig::from_env(&env);

    if !crate::ratelimit::check(&env, "RL_SIGNUP", &ip).await? {
        return Err(AppError::RateLimit(
            "signup rate limit exceeded".to_string(),
        ));
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
    let access_token = generate_access_token_string()?;
    let refresh_token = generate_refresh_token_string()?;

    let user = insert_user(&db, &email, &password_hash, &display_name, now).await?;
    let device = resolve_device(
        &db,
        user.id,
        payload.device_id.as_deref(),
        &device_name,
        &device_type,
        now,
    )
    .await?;

    let access_hash = hash_token_str(&access_token).to_vec();
    let refresh_hash = hash_token_str(&refresh_token).to_vec();

    let session = insert_session(
        &db,
        &NewSession {
            user_id: user.id,
            device_id: device.id,
            token_hash: &access_hash,
            refresh_hash: &refresh_hash,
            expires_at: now + ACCESS_TOKEN_TTL_SECS,
            refresh_expires_at: now + REFRESH_TOKEN_TTL_SECS,
            now,
        },
    )
    .await?;

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
    let ip = req
        .headers()
        .get("cf-connecting-ip")?
        .unwrap_or_else(|| "127.0.0.1".to_string());
    let now = chrono::Utc::now().timestamp();

    if !crate::ratelimit::check(&env, "RL_LOGIN", &ip).await? {
        return Err(AppError::RateLimit("login rate limit exceeded".to_string()));
    }

    let email = normalize_email(&payload.email)
        .ok_or_else(|| AppError::BadRequest("email is required".to_string()))?;
    if payload.password.trim().is_empty() {
        return Err(AppError::BadRequest("password is required".to_string()));
    }

    let db = env.d1("MAHO_DB")?;
    let mut user = find_user_by_email(&db, &email)
        .await?
        .ok_or_else(|| AppError::Auth("invalid credentials".to_string()))?;

    if user
        .locked_until
        .map(|locked_until| locked_until > now)
        .unwrap_or(false)
    {
        return Err(AppError::Auth("account is temporarily locked".to_string()));
    }

    // An OAuth-only account stores password_hash = ''. Reject before Argon2 so
    // verify_password is never reached with an empty stored hash.
    if user.password_hash.is_empty() {
        return Err(AppError::Auth("invalid credentials".to_string()));
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
        update_user_login_state(
            &db,
            user.id,
            user.failed_login_attempts,
            user.locked_until,
            user.last_failed_login_at,
        )
        .await?;

        return Err(AppError::Auth("invalid credentials".to_string()));
    }

    user.failed_login_attempts = 0;
    user.last_failed_login_at = None;
    user.locked_until = None;
    update_user_login_state(&db, user.id, 0, None, None).await?;

    let device_name = normalize_device_name(payload.device_name.as_deref());
    let device_type = normalize_device_type(payload.device_type.as_deref());
    let access_token = generate_access_token_string()?;
    let refresh_token = generate_refresh_token_string()?;

    let device = resolve_device(
        &db,
        user.id,
        payload.device_id.as_deref(),
        &device_name,
        &device_type,
        now,
    )
    .await?;

    let access_hash = hash_token_str(&access_token).to_vec();
    let refresh_hash = hash_token_str(&refresh_token).to_vec();

    let session = insert_session(
        &db,
        &NewSession {
            user_id: user.id,
            device_id: device.id,
            token_hash: &access_hash,
            refresh_hash: &refresh_hash,
            expires_at: now + ACCESS_TOKEN_TTL_SECS,
            refresh_expires_at: now + REFRESH_TOKEN_TTL_SECS,
            now,
        },
    )
    .await?;

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

pub async fn oauth_google(mut req: Request, env: Env) -> Result<Response> {
    let payload = req.json::<GoogleOAuthRequest>().await?;
    let ip = req
        .headers()
        .get("cf-connecting-ip")?
        .unwrap_or_else(|| "127.0.0.1".to_string());
    let now = chrono::Utc::now().timestamp();
    let config = RelayConfig::from_env(&env);

    if !crate::ratelimit::check(&env, "RL_LOGIN", &ip).await? {
        return Err(AppError::RateLimit("login rate limit exceeded".to_string()));
    }

    if payload.id_token.trim().is_empty() {
        return Err(AppError::BadRequest("id_token is required".to_string()));
    }

    let identity = google::verify_id_token(
        &payload.id_token,
        &config.google_oauth_allowed_auds,
        &payload.nonce,
        now,
    )
    .await?;

    let db = env.d1("MAHO_DB")?;

    let nonce_hash = google::hash_nonce(&identity.nonce).to_vec();
    if !claim_oauth_nonce(&db, &nonce_hash, now).await? {
        return Err(AppError::Auth("id_token already used".to_string()));
    }

    let user = match find_oauth_identity(&db, GOOGLE_PROVIDER, &identity.provider_sub).await? {
        Some(existing) => {
            touch_oauth_identity(&db, existing.id, now).await?;
            find_user_by_id(&db, existing.user_id)
                .await?
                .ok_or_else(|| AppError::Internal("linked user missing".to_string()))?
        }
        None => resolve_google_user(&db, &config, &identity, now).await?,
    };

    let device_name = normalize_device_name(payload.device_name.as_deref());
    let device_type = normalize_device_type(payload.device_type.as_deref());
    let access_token = generate_access_token_string()?;
    let refresh_token = generate_refresh_token_string()?;

    let device = resolve_device(
        &db,
        user.id,
        payload.device_id.as_deref(),
        &device_name,
        &device_type,
        now,
    )
    .await?;

    let access_hash = hash_token_str(&access_token).to_vec();
    let refresh_hash = hash_token_str(&refresh_token).to_vec();

    let session = insert_session(
        &db,
        &NewSession {
            user_id: user.id,
            device_id: device.id,
            token_hash: &access_hash,
            refresh_hash: &refresh_hash,
            expires_at: now + ACCESS_TOKEN_TTL_SECS,
            refresh_expires_at: now + REFRESH_TOKEN_TTL_SECS,
            now,
        },
    )
    .await?;

    let account = public_user_with_google_metadata(&db, user).await?;
    let auth_res = AuthResponse {
        access_token,
        refresh_token,
        token_type: "Bearer",
        expires_at: session.expires_at,
        refresh_expires_at: session.refresh_expires_at,
        account,
        device: PublicDevice::from(device),
        session: PublicSession::from(session),
    };

    Ok(Response::from_json(&auth_res)?)
}

/// Desktop loopback code exchange. The desktop client cannot hold the Google
/// client secret, so it posts the PKCE authorization code here; the relay
/// exchanges it server-side and returns only the `id_token`, which the client
/// then submits to `/auth/oauth/google` for audience/nonce verification and
/// session minting. Keeps the Google client secret off every client binary.
pub async fn oauth_google_exchange(mut req: Request, env: Env) -> Result<Response> {
    let payload = req.json::<GoogleExchangeRequest>().await?;
    let ip = req
        .headers()
        .get("cf-connecting-ip")?
        .unwrap_or_else(|| "127.0.0.1".to_string());

    if !crate::ratelimit::check(&env, "RL_LOGIN", &ip).await? {
        return Err(AppError::RateLimit("login rate limit exceeded".to_string()));
    }

    if payload.code.trim().is_empty()
        || payload.code_verifier.trim().is_empty()
        || payload.redirect_uri.trim().is_empty()
    {
        return Err(AppError::BadRequest(
            "code, code_verifier and redirect_uri are required".to_string(),
        ));
    }

    let config = RelayConfig::from_env(&env);
    let client_id = config
        .google_oauth_client_id
        .ok_or_else(|| AppError::Internal("google oauth client id not configured".to_string()))?;
    let client_secret = config.google_oauth_client_secret.ok_or_else(|| {
        AppError::Internal("google oauth client secret not configured".to_string())
    })?;

    let id_token = google::exchange_code(
        &client_id,
        &client_secret,
        &payload.code,
        &payload.code_verifier,
        &payload.redirect_uri,
    )
    .await?;

    Ok(Response::from_json(&GoogleExchangeResponse { id_token })?)
}

/// Resolves the account for a first-time Google identity: link to an existing
/// same-email account only when Google asserts the address is verified,
/// otherwise an attacker could pre-register an unverified Google address to
/// hijack a password account. When no account exists, create one.
async fn resolve_google_user(
    db: &D1Database,
    config: &RelayConfig,
    identity: &google::VerifiedIdentity,
    now: i64,
) -> Result<User> {
    if let Some(existing) = find_user_by_email(db, &identity.email).await? {
        if !identity.email_verified {
            return Err(AppError::Conflict(
                "email already exists; sign in with your password to link Google".to_string(),
            ));
        }
        link_oauth_identity(
            db,
            existing.id,
            GOOGLE_PROVIDER,
            &identity.provider_sub,
            &identity.email,
            now,
        )
        .await?;
        if existing.email_verified == 0.0 {
            mark_email_verified(db, existing.id).await?;
        }
        return find_user_by_id(db, existing.id)
            .await?
            .ok_or_else(|| AppError::Internal("linked user missing".to_string()));
    }

    if !config.open_registration {
        return Err(AppError::BadRequest("registration is disabled".to_string()));
    }

    let display_name = normalize_display_name(identity.display_name.as_deref(), &identity.email);
    let user = insert_oauth_user(
        db,
        &identity.email,
        &display_name,
        identity.email_verified,
        now,
    )
    .await?;
    link_oauth_identity(
        db,
        user.id,
        GOOGLE_PROVIDER,
        &identity.provider_sub,
        &identity.email,
        now,
    )
    .await?;
    Ok(user)
}

pub async fn logout(req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let auth = get_auth_context(&req, &db).await?;

    delete_session_by_id(&db, auth.session.id).await?;
    Ok(Response::from_json(&LogoutResponse { ok: true })?)
}

pub async fn refresh(mut req: Request, env: Env) -> Result<Response> {
    let ip = req
        .headers()
        .get("cf-connecting-ip")?
        .unwrap_or_else(|| "127.0.0.1".to_string());
    if !crate::ratelimit::check(&env, "RL_REFRESH", &ip).await? {
        return Err(AppError::RateLimit(
            "refresh rate limit exceeded".to_string(),
        ));
    }

    let payload = req.json::<RefreshRequest>().await?;
    let db = env.d1("MAHO_DB")?;
    let now = chrono::Utc::now().timestamp();

    if payload.refresh_token.trim().is_empty() {
        return Err(AppError::BadRequest(
            "refresh_token is required".to_string(),
        ));
    }

    let presented_refresh_hash = hash_token_str(&payload.refresh_token);
    let access_token = generate_access_token_string()?;
    let refresh_token = generate_refresh_token_string()?;

    let access_hash = hash_token_str(&access_token).to_vec();
    let refresh_hash = hash_token_str(&refresh_token).to_vec();

    let updated = rotate_session_tokens(
        &db,
        &presented_refresh_hash,
        &access_hash,
        &refresh_hash,
        now + ACCESS_TOKEN_TTL_SECS,
        now + REFRESH_TOKEN_TTL_SECS,
        now,
    )
    .await?;
    let updated = match updated {
        Some(updated) => updated,
        None => {
            if find_session_by_refresh_hash(&db, &presented_refresh_hash)
                .await?
                .is_some_and(|session| session.refresh_expires_at <= now)
            {
                return Err(AppError::Auth("refresh token expired".to_string()));
            }
            return Err(AppError::Auth("invalid refresh token".to_string()));
        }
    };

    let user = find_user_by_id(&db, updated.user_id)
        .await?
        .ok_or_else(|| AppError::Internal("user missing".to_string()))?;
    let device = find_device_by_id(&db, updated.device_id)
        .await?
        .ok_or_else(|| AppError::Internal("device missing".to_string()))?;
    touch_device(&db, device.id, now).await?;

    let account = public_user_with_google_metadata(&db, user).await?;
    let auth_res = AuthResponse {
        access_token,
        refresh_token,
        token_type: "Bearer",
        expires_at: updated.expires_at,
        refresh_expires_at: updated.refresh_expires_at,
        account,
        device: PublicDevice::from(device),
        session: PublicSession::from(updated),
    };

    Ok(Response::from_json(&auth_res)?)
}

pub async fn me(req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let auth = get_auth_context(&req, &db).await?;

    let devices = list_devices_for_user(&db, auth.user.id)
        .await?
        .into_iter()
        .map(PublicDevice::from)
        .collect();

    let account = public_user_with_google_metadata(&db, auth.user).await?;
    let me_res = MeResponse {
        account,
        devices,
        current_device: PublicDevice::from(auth.device),
        current_session: PublicSession::from(auth.session),
    };

    Ok(Response::from_json(&me_res)?)
}

pub async fn subscription(req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let auth = get_auth_context(&req, &db).await?;

    // D1 `first::<T>` deserializes a row as a JS object; a tuple type fails
    // with invalid_type. Named struct required (see r/AGENTS.md CONVENTIONS).
    #[derive(serde::Deserialize)]
    struct CreditsRow {
        balance_usd: f64,
        lifetime_purchased_usd: f64,
    }
    let stmt =
        db.prepare("SELECT balance_usd, lifetime_purchased_usd FROM credits WHERE user_id = ?1");
    let stmt = stmt.bind(&[js(&auth.user.id)?])?;
    let credits_row = stmt.first::<CreditsRow>(None).await?;
    let (balance, lifetime) = credits_row
        .map(|r| (r.balance_usd, r.lifetime_purchased_usd))
        .unwrap_or((0.0, 0.0));

    let sub_res = SubscriptionResponse {
        tier_ceiling_usd: crate::config::tier_ceiling_usd(&auth.user.tier),
        tier: auth.user.tier,
        subscription_status: auth.user.subscription_status,
        subscription_expires_at: auth.user.subscription_expires_at,
        period_start_at: auth.user.period_start_at,
        anniversary_reset_day: auth.user.anniversary_reset_day,
        credit_balance_usd: balance,
        lifetime_purchased_usd: lifetime,
        has_payment_method: auth.user.has_payment_method != 0.0,
    };

    Ok(Response::from_json(&sub_res)?)
}

pub async fn delete_account_route(req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let auth = get_auth_context(&req, &db).await?;

    delete_account(&db, auth.user.id, &auth.user.email).await?;
    Ok(Response::from_json(&DeleteAccountResponse { ok: true })?)
}

pub async fn delete_device(req: Request, env: Env, device_id_str: &str) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let auth = get_auth_context(&req, &db).await?;

    let device_id: i32 = device_id_str
        .parse()
        .map_err(|_| AppError::BadRequest("invalid device id".to_string()))?;

    let device = find_device_by_id(&db, device_id)
        .await?
        .ok_or_else(|| AppError::NotFound("device not found".to_string()))?;

    if device.user_id != auth.user.id {
        return Err(AppError::Auth(
            "device does not belong to current user".to_string(),
        ));
    }

    delete_device_and_sessions(&db, device.id).await?;
    Ok(Response::from_json(&DeleteDeviceResponse { ok: true })?)
}

pub async fn introspect(mut req: Request, env: Env) -> Result<Response> {
    let payload = req.json::<IntrospectRequest>().await?;

    require_service_token(&req, &env)?;

    let db = env.d1("MAHO_DB")?;
    let token_hash = hash_token_str(&payload.token);
    let context = find_active_session_context_by_access_hash(&db, &token_hash)
        .await?
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

    let resend_api_key = env
        .secret("RESEND_API_KEY")
        .map(|v| v.to_string())
        .ok()
        .filter(|k| !k.trim().is_empty());

    if let Some(api_key) = resend_api_key.as_deref() {
        send_verification_email(&env, api_key, &auth.user.email, &token).await?;
    }

    // SAFETY: the plaintext token proves email ownership. Returning it in the HTTP
    // response would let any authenticated caller self-verify without receiving the
    // email, defeating the ownership check. Expose it ONLY when MAHO_ENV is
    // explicitly set to a development/local value; all other values (including
    // unset) are treated as non-dev — FAIL CLOSED by default.
    let maho_env = env.var("MAHO_ENV").map(|v| v.to_string()).ok();
    let dev_token = if should_expose_dev_token(maho_env.as_deref()) {
        Some(token)
    } else {
        None
    };

    Ok(Response::from_json(&RequestVerifyResponse {
        status: "success".to_string(),
        dev_token,
    })?)
}

fn should_expose_dev_token(maho_env: Option<&str>) -> bool {
    matches!(
        maho_env.map(|s| s.trim().to_ascii_lowercase()).as_deref(),
        Some("development") | Some("local") | Some("dev")
    )
}

// Uses the Worker's outbound Fetch (as in billing.rs); reqwest is unavailable on
// the wasm32 Worker target. Send failures are logged and surfaced, not swallowed.
async fn send_verification_email(
    env: &Env,
    api_key: &str,
    to_email: &str,
    token: &str,
) -> Result<()> {
    let from_email = env
        .var("MAHO_FROM_EMAIL")
        .map(|v| v.to_string())
        .ok()
        .filter(|v| !v.trim().is_empty())
        .unwrap_or_else(|| "Maho <noreply@mahobrowser.com>".to_string());

    let verify_base = env
        .var("MAHO_VERIFY_URL")
        .map(|v| v.to_string())
        .ok()
        .filter(|v| !v.trim().is_empty())
        .unwrap_or_else(|| "https://mahobrowser.com/verify".to_string());

    // Token is a 32-char lowercase-hex UUID (`.simple()`), already URL-safe.
    let verify_link = format!("{verify_base}?token={token}");

    let html = format!(
        "<p>Confirm your Maho email address to finish setting up your account.</p>\
         <p><a href=\"{verify_link}\">Verify my email</a></p>\
         <p>Or paste this link into your browser:<br>{verify_link}</p>\
         <p>This link expires in 24 hours. If you didn't request it, you can ignore this email.</p>"
    );

    #[derive(Serialize)]
    struct ResendEmail<'a> {
        from: &'a str,
        to: [&'a str; 1],
        subject: &'a str,
        html: &'a str,
    }

    let body = serde_json::to_string(&ResendEmail {
        from: &from_email,
        to: [to_email],
        subject: "Verify your Maho email",
        html: &html,
    })
    .map_err(|e| AppError::Internal(format!("failed to serialize verification email: {e}")))?;

    let headers = Headers::new();
    headers.set("Content-Type", "application/json")?;
    headers.set("Authorization", &format!("Bearer {api_key}"))?;

    let mut init = RequestInit::new();
    init.with_method(Method::Post)
        .with_headers(headers)
        .with_body(Some(body.into()));

    let request = Request::new_with_init("https://api.resend.com/emails", &init)?;

    match Fetch::Request(request).send().await {
        Ok(resp) if (200..300).contains(&resp.status_code()) => Ok(()),
        Ok(mut resp) => {
            let status = resp.status_code();
            let detail = resp.text().await.unwrap_or_default();
            console_error!("Resend verify email failed: status={status} body={detail}");
            Err(AppError::Internal(format!(
                "verification email send failed (status {status})"
            )))
        }
        Err(e) => {
            console_error!("Resend verify email fetch error: {e}");
            Err(AppError::Internal(format!(
                "verification email send error: {e}"
            )))
        }
    }
}

pub async fn verify(mut req: Request, env: Env) -> Result<Response> {
    let payload = req.json::<VerifyRequest>().await?;
    let now = chrono::Utc::now().timestamp();
    let db = env.d1("MAHO_DB")?;

    let hashed = format!("{:x}", sha2::Sha256::digest(payload.token.as_bytes()));
    let stmt = db.prepare(
        "SELECT * FROM users WHERE verification_token = ?1 AND verification_token_expires_at > ?2",
    );
    let stmt = stmt.bind(&[js(&hashed)?, js(&now)?])?;
    let user = stmt
        .first::<User>(None)
        .await?
        .ok_or_else(|| AppError::BadRequest("invalid or expired verification token".to_string()))?;

    let stmt2 = db.prepare("UPDATE users SET email_verified = 1, verification_token = NULL, verification_token_expires_at = NULL WHERE id = ?1");
    let stmt2 = stmt2.bind(&[js(&user.id)?])?;
    stmt2.run().await?;

    let service_token = env
        .secret("MAHO_RELAY_SERVICE_TOKEN")
        .map(|v| v.to_string())
        .unwrap_or_default();

    let bypass_token = generate_bypass_token(user.id, &service_token)?;

    Ok(Response::from_json(&VerifyResponse {
        status: "success".to_string(),
        verify_bypass_token: bypass_token,
    })?)
}

pub async fn consume_bypass(mut req: Request, env: Env) -> Result<Response> {
    let payload = req.json::<ConsumeBypassRequest>().await?;

    require_service_token(&req, &env)?;

    let expected_service_token = env
        .secret("MAHO_RELAY_SERVICE_TOKEN")
        .map(|v| v.to_string())
        .unwrap_or_default();

    let (user_id, _expires_at) = parse_bypass_token(&payload.token, &expected_service_token)
        .ok_or_else(|| AppError::Auth("invalid or expired bypass token".to_string()))?;

    let db = env.d1("MAHO_DB")?;
    let token_hash = sha2::Sha256::digest(payload.token.as_bytes()).to_vec();
    let now = chrono::Utc::now().timestamp();

    let stmt_insert = db.prepare(
        "INSERT INTO consumed_bypass_tokens (token_hash, user_id, consumed_at) VALUES (?1, ?2, ?3)
         ON CONFLICT(token_hash) DO NOTHING
         RETURNING user_id",
    );
    let stmt_insert = stmt_insert.bind(&[js(&token_hash)?, js(&user_id)?, js(&now)?])?;
    let claimed = stmt_insert.first::<i32>(Some("user_id")).await?.is_some();

    if !claimed {
        return Err(AppError::BadRequest(
            "bypass token already consumed".to_string(),
        ));
    }

    Ok(Response::from_json(&ConsumeBypassResponse { user_id })?)
}

pub async fn admin_force_verify_email(mut req: Request, env: Env) -> Result<Response> {
    let payload = req.json::<AdminForceVerifyEmailRequest>().await?;

    require_admin_token(&req, &env)?;

    if payload.reason.trim().is_empty() {
        return Err(AppError::BadRequest("reason required".to_string()));
    }

    let db = env.d1("MAHO_DB")?;
    let now = chrono::Utc::now().timestamp();
    let exists = find_user_by_id(&db, payload.user_id).await?.is_some();
    if !exists {
        return Err(AppError::NotFound("user not found".to_string()));
    }

    let stmt = db.prepare("UPDATE users SET email_verified = 1 WHERE id = ?1");
    let stmt = stmt.bind(&[js(&payload.user_id)?])?;
    stmt.run().await?;

    insert_admin_audit(
        &db,
        "force_verify_email",
        payload.user_id,
        &payload.reason,
        None,
        now,
    )
    .await?;

    Ok(Response::from_json(&AdminForceVerifyEmailResponse {
        user_id: payload.user_id,
        email_verified: true,
    })?)
}

pub async fn admin_set_credit_balance(mut req: Request, env: Env) -> Result<Response> {
    let payload = req.json::<AdminSetCreditBalanceRequest>().await?;

    require_admin_token(&req, &env)?;

    if payload.reason.trim().is_empty() {
        return Err(AppError::BadRequest("reason required".to_string()));
    }

    let db = env.d1("MAHO_DB")?;
    let now = chrono::Utc::now().timestamp();

    let prev_balance =
        set_credit_balance(&db, payload.user_id, payload.new_balance_usd, now).await?;
    let delta = payload.new_balance_usd - prev_balance;

    let detail = format!("{prev_balance:.4}->{:.4}", payload.new_balance_usd);
    insert_admin_audit(
        &db,
        "set_credit_balance",
        payload.user_id,
        &payload.reason,
        Some(&detail),
        now,
    )
    .await?;

    crate::billing::notify_proxy_credits_update(
        &env,
        payload.user_id,
        payload.new_balance_usd,
        delta,
    )
    .await;

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

    let payload = format!("{user_id_str}:{expires_at_str}");
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

fn generate_bypass_token(user_id: i32, service_token: &str) -> Result<String> {
    let expires_at = chrono::Utc::now().timestamp() + 300; // 5 minutes
    let payload = format!("{user_id}:{expires_at}");
    let mut mac = Hmac::<Sha256>::new_from_slice(service_token.as_bytes())
        .map_err(|_| AppError::Internal("failed to initialize bypass token signer".to_string()))?;
    mac.update(payload.as_bytes());
    let sig = format!("{:x}", mac.finalize().into_bytes());
    Ok(format!("{user_id}:{expires_at}:{sig}"))
}

async fn public_user_with_google_metadata(db: &D1Database, user: User) -> Result<PublicUser> {
    let metadata = find_google_account_metadata(db, user.id).await?;
    Ok(account_with_google_metadata(
        PublicUser::from(user),
        metadata,
    ))
}

fn account_with_google_metadata(
    mut account: PublicUser,
    metadata: Option<OAuthAccountMetadata>,
) -> PublicUser {
    if let Some(metadata) = metadata {
        account.oauth_provider = Some(metadata.provider);
        account.oauth_provider_sub = Some(metadata.provider_sub);
    }
    account
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
            oauth_provider: None,
            oauth_provider_sub: None,
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

#[cfg(test)]
mod tests {
    use super::account_with_google_metadata;
    use super::service_token_matches;
    use super::should_expose_dev_token;
    use crate::auth::models::PublicUser;
    use crate::db::OAuthAccountMetadata;

    fn public_user() -> PublicUser {
        PublicUser {
            id: 7,
            email: "account@example.com".to_string(),
            display_name: "Account User".to_string(),
            created_at: 1_700_000_000,
            locked_until: None,
            failed_login_attempts: 0,
            last_failed_login_at: None,
            oauth_provider: None,
            oauth_provider_sub: None,
        }
    }

    #[test]
    fn refresh_account_exposes_canonical_google_identity_pair() {
        let account = account_with_google_metadata(
            public_user(),
            Some(OAuthAccountMetadata {
                provider: "google".to_string(),
                provider_sub: "canonical-sub".to_string(),
            }),
        );

        assert_eq!(account.oauth_provider.as_deref(), Some("google"));
        assert_eq!(account.oauth_provider_sub.as_deref(), Some("canonical-sub"));
    }

    #[test]
    fn refresh_account_omits_google_identity_pair_when_unlinked() {
        let account = account_with_google_metadata(public_user(), None);

        assert_eq!(account.oauth_provider, None);
        assert_eq!(account.oauth_provider_sub, None);
    }

    #[test]
    fn service_token_matches_validation() {
        // Mismatches and empty/whitespace checks
        assert!(!service_token_matches("provided", ""));
        assert!(!service_token_matches("provided", "   "));
        assert!(!service_token_matches("", "expected"));
        assert!(!service_token_matches("   ", "expected"));
        assert!(!service_token_matches("", ""));
        assert!(!service_token_matches("   ", "   "));

        // Match case
        assert!(service_token_matches("token123", "token123"));
        // Mismatch case
        assert!(!service_token_matches("token123", "token456"));
    }

    #[test]
    fn dev_token_hidden_when_env_unset() {
        // FAIL CLOSED: unset MAHO_ENV must not expose token.
        assert!(!should_expose_dev_token(None));
    }

    #[test]
    fn dev_token_hidden_when_env_production() {
        assert!(!should_expose_dev_token(Some("production")));
        assert!(!should_expose_dev_token(Some("staging")));
        assert!(!should_expose_dev_token(Some("PRODUCTION")));
        assert!(!should_expose_dev_token(Some("")));
        assert!(!should_expose_dev_token(Some("   ")));
    }

    #[test]
    fn dev_token_exposed_when_env_dev() {
        assert!(should_expose_dev_token(Some("development")));
        assert!(should_expose_dev_token(Some("local")));
        assert!(should_expose_dev_token(Some("dev")));
        // case-insensitive
        assert!(should_expose_dev_token(Some("Development")));
        assert!(should_expose_dev_token(Some("LOCAL")));
    }

    /// Regression: the original fail-open bug — MAHO_ENV unset AND no RESEND key
    /// (e.g. fresh-env deploy with missing secret) must never leak the token.
    #[test]
    fn dev_token_hidden_when_env_and_key_both_unset() {
        // Simulates: env.var("MAHO_ENV") -> Err (unset), env.secret("RESEND_API_KEY") -> Err (unset)
        // The gate now depends solely on MAHO_ENV; unset = fail closed.
        assert!(!should_expose_dev_token(None));
    }
}
