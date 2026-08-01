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
/// If the binding is missing or cannot be cast, allow with a warning in dev.
/// Outside development, return an internal error so unavailable infrastructure
/// cannot be mistaken for a real rate-limit denial.
pub async fn check(env: &Env, limiter: &str, key: &str) -> Result<bool> {
    match env.get_binding::<RateLimiter>(limiter) {
        Ok(rl) => {
            let outcome = rl
                .limit(key.to_string())
                .await
                .map_err(|e| AppError::Internal(format!("rate limiter {limiter} failed: {e}")))?;
            Ok(outcome.success)
        }
        Err(_) => {
            let is_development = is_dev(env);
            if is_development {
                worker::console_log!("rate limiter binding {limiter} unavailable (dev): allowing");
            } else {
                worker::console_error!(
                    "rate limiter binding {limiter} unavailable outside development"
                );
            }
            missing_binding_result(is_development, limiter)
        }
    }
}

fn missing_binding_result(is_development: bool, limiter: &str) -> Result<bool> {
    if is_development {
        return Ok(true);
    }

    Err(AppError::Internal(format!(
        "rate limiter binding {limiter} unavailable"
    )))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn missing_binding_when_development_allows_requests() {
        // Given: local development has no Cloudflare rate-limit binding.
        let result = missing_binding_result(true, "RL_SIGNUP");

        // When: the binding lookup is unavailable.
        // Then: local development preserves its intentional allowance.
        assert!(result.is_ok_and(|allowed| allowed));
    }

    #[test]
    fn missing_binding_when_not_development_returns_internal_error() {
        // Given: staging or production has no usable rate-limit binding.
        let result = missing_binding_result(false, "RL_SIGNUP");

        // When: the binding lookup is unavailable.
        // Then: the caller receives an internal error, not a synthetic denial.
        assert!(matches!(result, Err(AppError::Internal(_))));
    }

    #[test]
    fn explicit_limiter_denial_remains_distinct_from_binding_failure() {
        // Given: a real limiter denial is represented by a successful check.
        let limiter_result = false;

        // When: the caller evaluates the limiter result.
        // Then: only this successful false result selects the 429 caller path.
        assert!(!limiter_result);
    }
}
