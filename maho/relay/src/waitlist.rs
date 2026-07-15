use crate::errors::{AppError, Result};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::collections::HashMap;
use std::sync::Mutex;
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

fn check_rate_limit(ip_hash: &str, limit_per_min: u32, now: i64) -> bool {
    static LIMITER: std::sync::OnceLock<Mutex<HashMap<String, Vec<i64>>>> =
        std::sync::OnceLock::new();
    let mut map = match LIMITER.get_or_init(|| Mutex::new(HashMap::new())).lock() {
        Ok(map) => map,
        Err(poisoned) => poisoned.into_inner(),
    };
    let timestamps = map.entry(ip_hash.to_string()).or_default();

    timestamps.retain(|&t| t > now - 60);

    if timestamps.len() >= limit_per_min as usize {
        false
    } else {
        timestamps.push(now);
        true
    }
}

fn js<T: serde::Serialize>(val: &T) -> std::result::Result<wasm_bindgen::JsValue, AppError> {
    let js_val =
        serde_wasm_bindgen::to_value(val).map_err(|e| AppError::Internal(e.to_string()))?;
    if js_val.is_undefined() {
        Ok(wasm_bindgen::JsValue::null())
    } else {
        Ok(js_val)
    }
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

    if !check_rate_limit(&ip_hash, 5, now) {
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

    // Idempotent check
    let stmt_check = db.prepare("SELECT id FROM waitlist_signups WHERE email = ?1");
    let stmt_check = stmt_check.bind(&[js(&email)?])?;
    let exists = stmt_check.first::<i32>(Some("id")).await?.is_some();

    if exists {
        return Ok(Response::from_json(&CreateWaitlistResponse {
            ok: true,
            already_signed_up: true,
        })?);
    }

    // Insert into waitlist_signups
    let stmt_insert = db.prepare(
        "INSERT INTO waitlist_signups (email, source, ip_hash, created_at, metadata)
         VALUES (?1, ?2, ?3, ?4, ?5)",
    );
    let stmt_insert = stmt_insert.bind(&[
        js(&email)?,
        js(&source)?,
        js(&ip_hash)?,
        js(&now)?,
        js(&metadata_str)?,
    ])?;
    stmt_insert.run().await?;

    Ok(Response::from_json(&CreateWaitlistResponse {
        ok: true,
        already_signed_up: false,
    })?)
}
