use crate::db::js;
use crate::errors::{AppError, Result};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use worker::*;

const VALID_SOURCES: &[&str] = &["browser", "mail", "general"];

#[derive(Debug, Deserialize)]
pub struct CreateWaitlistRequest {
    pub email: String,
    pub source: Option<String>,
    pub metadata: Option<serde_json::Value>,
}

#[derive(Debug, Serialize)]
pub struct CreateWaitlistResponse {
    pub ok: bool,
    pub already_signed_up: bool,
}

pub async fn handler(mut req: Request, env: Env) -> Result<Response> {
    let payload = req.json::<CreateWaitlistRequest>().await?;
    let ip = req
        .headers()
        .get("cf-connecting-ip")?
        .unwrap_or_else(|| "127.0.0.1".to_string());
    let now = chrono::Utc::now().timestamp();

    // Hash IP address
    let mut hasher = Sha256::new();
    hasher.update(ip.as_bytes());
    let ip_hash = format!("{:x}", hasher.finalize());

    if !crate::ratelimit::check(&env, "RL_WAITLIST", &ip_hash).await? {
        return Err(AppError::RateLimit("rate_limit_exceeded".to_string()));
    }

    // Validate email
    let email = payload.email.trim().to_lowercase();
    if email.is_empty() {
        return Err(AppError::BadRequest("missing_email".to_string()));
    }
    if email.len() > 254 || !email.contains('@') || email.len() < 3 {
        return Err(AppError::BadRequest("invalid_email".to_string()));
    }

    // Validate source
    let source = payload.source.as_deref().unwrap_or("general").to_string();
    if !VALID_SOURCES.contains(&source.as_str()) {
        return Err(AppError::BadRequest("invalid_source".to_string()));
    }

    let metadata_str = match payload.metadata {
        Some(ref v) => {
            let s = serde_json::to_string(v).map_err(|e| AppError::Internal(e.to_string()))?;
            if s.len() > 10240 {
                // 10 KB limit
                return Err(AppError::BadRequest("metadata_too_large".to_string()));
            }
            Some(s)
        }
        None => None,
    };

    let db = env.d1("MAHO_DB")?;

    // Dedup on the full (email, source) unique index (not email alone), and a
    // single conditional insert removes the check-then-insert race.
    let stmt_insert = db.prepare(
        "INSERT INTO waitlist_signups (email, source, ip_hash, created_at, metadata)
         VALUES (?1, ?2, ?3, ?4, ?5)
         ON CONFLICT(email, source) DO NOTHING
         RETURNING id",
    );
    let stmt_insert = stmt_insert.bind(&[
        js(&email)?,
        js(&source)?,
        js(&ip_hash)?,
        js(&now)?,
        js(&metadata_str)?,
    ])?;
    let inserted = stmt_insert.first::<i32>(Some("id")).await?.is_some();

    Ok(Response::from_json(&CreateWaitlistResponse {
        ok: true,
        already_signed_up: !inserted,
    })?)
}
