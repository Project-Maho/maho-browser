// Copyright 2026 Maho Browser. All rights reserved.

//! Centralized error classification and bounded retry policy contracts.
//!
//! Enforces safety invariants:
//! - Never auto-retry uncertain external mutations
//! - Never auto-retry permanent policy denials
//! - Bounded exponential backoff with jitter (seeded RNG for deterministic testing)
//! - Max attempts cap then give up with reason
//! - Fallback-switch reason recorded when policy directs switching provider/tool

use serde::{Deserialize, Serialize};

/// High-level error classification for agent operations.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ErrorClass {
    /// Transient network error (connection drop, TCP reset, DNS timeout).
    TransientNetwork,
    /// Upstream rate limiting (HTTP 429).
    RateLimited,
    /// Context window limit exceeded (HTTP 400 with context_too_large).
    ContextTooLarge,
    /// Explicit security or permission policy rejection.
    PolicyDenied,
    /// Unrecoverable permanent failure (invalid parameters, 404, auth revoked).
    PermanentFailure,
    /// External mutation whose outcome cannot be verified.
    UncertainMutation,
}

impl ErrorClass {
    pub fn is_permanent(&self) -> bool {
        matches!(self, Self::PolicyDenied | Self::PermanentFailure)
    }

    pub fn is_uncertain_mutation(&self) -> bool {
        matches!(self, Self::UncertainMutation)
    }
}

/// Idempotency classification of an operation.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum IdempotencyClass {
    /// Safe to retry without side effects.
    ReadOnly,
    /// Safe to retry with idempotent request token.
    IdempotentMutation,
    /// Not safe to auto-retry; requires user confirmation or read verification.
    NonIdempotentMutation,
}

/// Configuration for bounded exponential backoff with jitter.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct BoundedBackoffConfig {
    pub initial_backoff_ms: u64,
    pub max_backoff_ms: u64,
    pub backoff_factor: f64,
    pub jitter_ratio: f64,
}

impl Default for BoundedBackoffConfig {
    fn default() -> Self {
        Self {
            initial_backoff_ms: 500,
            max_backoff_ms: 10_000,
            backoff_factor: 2.0,
            jitter_ratio: 0.2,
        }
    }
}

impl BoundedBackoffConfig {
    /// Calculate the bounded exponential backoff for a given attempt (1-based),
    /// applying jitter via a deterministic factor `jitter_factor` in [0.0, 1.0].
    pub fn calculate_backoff(&self, attempt: u32, jitter_factor: f64) -> u64 {
        if attempt == 0 {
            return 0;
        }
        let exp = (attempt - 1) as i32;
        let base_backoff = (self.initial_backoff_ms as f64) * self.backoff_factor.powi(exp);
        let clamped = base_backoff.min(self.max_backoff_ms as f64);

        let jitter_range = clamped * self.jitter_ratio;
        let jitter_offset = (jitter_factor * 2.0 - 1.0) * jitter_range;
        let final_backoff = (clamped + jitter_offset).max(0.0);
        (final_backoff as u64).min(self.max_backoff_ms)
    }
}

/// Reason explaining why an operation switched fallback strategies/providers/tools.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct FallbackSwitchReason {
    pub original_target: String,
    pub fallback_target: String,
    pub error_class: ErrorClass,
    pub attempts_made: u32,
    pub diagnostic: String,
}

/// Outcome decision after evaluating an error against retry policy.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum RetryDecision {
    /// Operation should be retried after the specified delay in milliseconds.
    RetryAfter(u64),
    /// Operation should fail immediately with the given diagnostic reason.
    FailFast(String),
    /// Operation requires explicit user confirmation before retry.
    RequiresConfirmation,
    /// Operation should switch to a fallback strategy/target with recorded reason.
    SwitchFallback(FallbackSwitchReason),
}

/// Evaluation result when policy refuses auto-retry.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DoNotRetryReason {
    /// Security/permission policy denied.
    PolicyDenied(String),
    /// Permanent unrecoverable failure.
    PermanentFailure(String),
    /// Attempt cap reached.
    MaxAttemptsExceeded { attempts: u32, max_attempts: u32 },
    /// Uncertain mutation requires fresh read/proof or user confirmation.
    NeedsResolution(String),
    /// Non-retryable error class under configured policy.
    NonRetryableClass(ErrorClass),
}

/// Policy governing retry attempts for operations.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct RetryPolicy {
    pub max_attempts: u32,
    pub backoff: BoundedBackoffConfig,
    pub retryable_classes: Vec<ErrorClass>,
    /// Guardrail: must remain false for uncertain mutations unless explicitly safe.
    pub allow_uncertain_mutation_retry: bool,
}

impl Default for RetryPolicy {
    fn default() -> Self {
        Self {
            max_attempts: 3,
            backoff: BoundedBackoffConfig::default(),
            retryable_classes: vec![ErrorClass::TransientNetwork, ErrorClass::RateLimited],
            allow_uncertain_mutation_retry: false,
        }
    }
}

/// Simple deterministic pseudo-random number generator for tests and jitter without external dependencies.
#[derive(Debug, Clone)]
pub struct DeterministicRng {
    state: u64,
}

impl DeterministicRng {
    pub fn seeded(seed: u64) -> Self {
        Self {
            state: if seed == 0 { 0x853c49e6748fea9b } else { seed },
        }
    }

    /// Generates next pseudo-random float in [0.0, 1.0).
    pub fn next_f64(&mut self) -> f64 {
        // xorshift64*
        let mut x = self.state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        self.state = x;
        let val = x.wrapping_mul(0x2545F4914F6CDD1D);
        ((val >> 11) as f64) / ((1u64 << 53) as f64)
    }
}

impl RetryPolicy {
    pub fn new(max_attempts: u32, backoff: BoundedBackoffConfig) -> Self {
        Self {
            max_attempts,
            backoff,
            retryable_classes: vec![ErrorClass::TransientNetwork, ErrorClass::RateLimited],
            allow_uncertain_mutation_retry: false,
        }
    }

    /// Evaluates whether an error should be retried, failed, confirmed, or switched to fallback.
    pub fn evaluate(
        &self,
        error_class: ErrorClass,
        idempotency: IdempotencyClass,
        current_attempt: u32,
        jitter_factor: f64,
    ) -> Result<RetryDecision, DoNotRetryReason> {
        // Guardrail 1: PolicyDenied and PermanentFailure are NEVER retryable
        if error_class == ErrorClass::PolicyDenied {
            return Err(DoNotRetryReason::PolicyDenied(
                "Action rejected by policy - auto-retry strictly forbidden".to_string(),
            ));
        }
        if error_class == ErrorClass::PermanentFailure {
            return Err(DoNotRetryReason::PermanentFailure(
                "Unrecoverable permanent failure - auto-retry forbidden".to_string(),
            ));
        }

        // Guardrail 2: Uncertain mutation is NEVER auto-retried without resolution/confirmation
        if error_class == ErrorClass::UncertainMutation
            || (idempotency == IdempotencyClass::NonIdempotentMutation
                && error_class != ErrorClass::ContextTooLarge)
        {
            if !self.allow_uncertain_mutation_retry || error_class == ErrorClass::UncertainMutation
            {
                return Err(DoNotRetryReason::NeedsResolution(
                    "Uncertain mutation requires fresh read verification or explicit user confirmation before retry".to_string(),
                ));
            }
        }

        // Guardrail 3: Check attempt cap (1-based current_attempt; max_attempts is the total attempts allowed)
        if current_attempt >= self.max_attempts {
            return Err(DoNotRetryReason::MaxAttemptsExceeded {
                attempts: current_attempt,
                max_attempts: self.max_attempts,
            });
        }

        // Check if class is retryable
        if !self.retryable_classes.contains(&error_class) {
            return Err(DoNotRetryReason::NonRetryableClass(error_class));
        }

        // Calculate bounded backoff with jitter
        let delay_ms = self
            .backoff
            .calculate_backoff(current_attempt, jitter_factor);
        Ok(RetryDecision::RetryAfter(delay_ms))
    }

    /// Evaluate with fallback switch recommendation if consecutive failures suggest provider switch.
    pub fn evaluate_with_fallback(
        &self,
        error_class: ErrorClass,
        idempotency: IdempotencyClass,
        current_attempt: u32,
        jitter_factor: f64,
        original_target: &str,
        fallback_target: &str,
    ) -> RetryDecision {
        match self.evaluate(error_class, idempotency, current_attempt, jitter_factor) {
            Ok(decision) => decision,
            Err(DoNotRetryReason::MaxAttemptsExceeded { attempts, .. }) => {
                RetryDecision::SwitchFallback(FallbackSwitchReason {
                    original_target: original_target.to_string(),
                    fallback_target: fallback_target.to_string(),
                    error_class,
                    attempts_made: attempts,
                    diagnostic: format!("Max retry attempts ({attempts}) exhausted on {original_target}; switching to fallback {fallback_target}"),
                })
            }
            Err(DoNotRetryReason::NeedsResolution(_msg)) => {
                RetryDecision::RequiresConfirmation
            }
            Err(DoNotRetryReason::PolicyDenied(reason)) => {
                RetryDecision::FailFast(reason)
            }
            Err(DoNotRetryReason::PermanentFailure(reason)) => {
                RetryDecision::FailFast(reason)
            }
            Err(DoNotRetryReason::NonRetryableClass(class)) => {
                RetryDecision::FailFast(format!("Non-retryable error class: {class:?}"))
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_classify_permanent_and_policy_denied_not_retryable() {
        let policy = RetryPolicy::default();

        // PolicyDenied -> DoNotRetryReason::PolicyDenied
        let res_policy =
            policy.evaluate(ErrorClass::PolicyDenied, IdempotencyClass::ReadOnly, 1, 0.5);
        assert!(matches!(res_policy, Err(DoNotRetryReason::PolicyDenied(_))));

        // PermanentFailure -> DoNotRetryReason::PermanentFailure
        let res_perm = policy.evaluate(
            ErrorClass::PermanentFailure,
            IdempotencyClass::ReadOnly,
            1,
            0.5,
        );
        assert!(matches!(
            res_perm,
            Err(DoNotRetryReason::PermanentFailure(_))
        ));
    }

    #[test]
    fn test_classify_transient_and_rate_limited_retryable_with_bounded_backoff_and_seeded_jitter() {
        let config = BoundedBackoffConfig {
            initial_backoff_ms: 100,
            max_backoff_ms: 1000,
            backoff_factor: 2.0,
            jitter_ratio: 0.2,
        };
        let policy = RetryPolicy::new(3, config.clone());

        let mut rng = DeterministicRng::seeded(42);

        // Attempt 1: base = 100ms, jitter in [80, 120]
        let jitter1 = rng.next_f64();
        let res1 = policy.evaluate(
            ErrorClass::TransientNetwork,
            IdempotencyClass::ReadOnly,
            1,
            jitter1,
        );
        assert!(res1.is_ok());
        if let Ok(RetryDecision::RetryAfter(delay)) = res1 {
            assert!(delay >= 80 && delay <= 120, "delay was {delay}");
        } else {
            panic!("Expected RetryAfter");
        }

        // Attempt 2: base = 200ms, jitter in [160, 240]
        let jitter2 = rng.next_f64();
        let res2 = policy.evaluate(
            ErrorClass::RateLimited,
            IdempotencyClass::ReadOnly,
            2,
            jitter2,
        );
        assert!(res2.is_ok());
        if let Ok(RetryDecision::RetryAfter(delay)) = res2 {
            assert!(delay >= 160 && delay <= 240, "delay was {delay}");
        } else {
            panic!("Expected RetryAfter");
        }

        // Repeat with same seed produces identical deterministic backoff sequence
        let mut rng_reproduced = DeterministicRng::seeded(42);
        let d1 = config.calculate_backoff(1, rng_reproduced.next_f64());
        let _d2 = config.calculate_backoff(2, rng_reproduced.next_f64());
        if let Ok(RetryDecision::RetryAfter(delay1)) = policy.evaluate(
            ErrorClass::TransientNetwork,
            IdempotencyClass::ReadOnly,
            1,
            DeterministicRng::seeded(42).next_f64(),
        ) {
            assert_eq!(d1, delay1);
        }
    }

    #[test]
    fn test_max_attempts_cap_then_give_up() {
        let policy = RetryPolicy {
            max_attempts: 3,
            ..Default::default()
        };

        // Attempt 1 and 2 succeed
        assert!(policy
            .evaluate(
                ErrorClass::TransientNetwork,
                IdempotencyClass::ReadOnly,
                1,
                0.5
            )
            .is_ok());
        assert!(policy
            .evaluate(
                ErrorClass::TransientNetwork,
                IdempotencyClass::ReadOnly,
                2,
                0.5
            )
            .is_ok());

        // Attempt 3 reaches max_attempts (3) -> giving up
        let res3 = policy.evaluate(
            ErrorClass::TransientNetwork,
            IdempotencyClass::ReadOnly,
            3,
            0.5,
        );
        assert_eq!(
            res3,
            Err(DoNotRetryReason::MaxAttemptsExceeded {
                attempts: 3,
                max_attempts: 3,
            })
        );
    }

    #[test]
    fn test_uncertain_mutation_never_auto_retries() {
        let policy = RetryPolicy::default();

        // Uncertain mutation with ReadOnly idempotency class
        let res1 = policy.evaluate(
            ErrorClass::UncertainMutation,
            IdempotencyClass::ReadOnly,
            1,
            0.5,
        );
        assert!(matches!(res1, Err(DoNotRetryReason::NeedsResolution(_))));

        // NonIdempotentMutation with TransientNetwork
        let res2 = policy.evaluate(
            ErrorClass::TransientNetwork,
            IdempotencyClass::NonIdempotentMutation,
            1,
            0.5,
        );
        assert!(matches!(res2, Err(DoNotRetryReason::NeedsResolution(_))));

        // Evaluate with fallback converts NeedsResolution to RequiresConfirmation
        let fallback_dec = policy.evaluate_with_fallback(
            ErrorClass::UncertainMutation,
            IdempotencyClass::NonIdempotentMutation,
            1,
            0.5,
            "provider-a",
            "provider-b",
        );
        assert_eq!(fallback_dec, RetryDecision::RequiresConfirmation);
    }

    #[test]
    fn test_fallback_switch_reason_recorded_on_exhaustion() {
        let policy = RetryPolicy {
            max_attempts: 3,
            ..Default::default()
        };

        let decision = policy.evaluate_with_fallback(
            ErrorClass::TransientNetwork,
            IdempotencyClass::ReadOnly,
            3,
            0.5,
            "primary-model",
            "fallback-model",
        );

        match decision {
            RetryDecision::SwitchFallback(reason) => {
                assert_eq!(reason.original_target, "primary-model");
                assert_eq!(reason.fallback_target, "fallback-model");
                assert_eq!(reason.error_class, ErrorClass::TransientNetwork);
                assert_eq!(reason.attempts_made, 3);
                assert!(reason.diagnostic.contains("Max retry attempts"));
            }
            other => panic!("Expected SwitchFallback, got {other:?}"),
        }
    }
}
