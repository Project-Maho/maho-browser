//! Edge-backed rate limiting via Cloudflare Rate Limiting bindings.
//!
//! Replaces the old in-memory OnceLock counters, which were isolate-local and
//! therefore ineffective (Workers runs many isolates per colo, so a per-isolate
//! HashMap never shares a count). Binding names must match the rate-limit
//! bindings declared in wrangler.toml.

use crate::errors::{AppError, Result};
use worker::{Env, RateLimiter};

fn is_dev(env: &Env) -> bool {
    matches!(
        env.var("MAHO_ENV")
            .map(|v| v.to_string().trim().to_ascii_lowercase())
            .ok()
            .as_deref(),
        Some("development") | Some("local") | Some("dev")
    )
}

/// `Ok(true)` = within limit, `Ok(false)` = reject with 429.
///
/// If the binding is missing, allow with a warning in dev but FAIL CLOSED
/// (reject) in production, so a misconfigured deploy cannot silently disable
/// rate limiting. Mirrors the dev-token fail-closed gate.
pub async fn check(env: &Env, limiter: &str, key: &str) -> Result<bool> {
    match env.get_binding::<RateLimiter>(limiter) {
        Ok(rl) => {
            let outcome = rl.limit(key.to_string()).await.map_err(|e| {
                AppError::Internal(format!("rate limiter {limiter} failed: {e}"))
            })?;
            Ok(outcome.success)
        }
        Err(_) if is_dev(env) => {
            worker::console_log!("rate limiter binding {limiter} missing (dev): allowing");
            Ok(true)
        }
        Err(_) => {
            worker::console_error!(
                "rate limiter binding {limiter} missing in production: rejecting (fail-closed)"
            );
            Ok(false)
        }
    }
}
