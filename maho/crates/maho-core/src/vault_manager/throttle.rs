//! Deterministic failed-unlock rate limiting.
//!
//! Time is supplied by the caller (explicit timestamp seam) so behavior is fully
//! deterministic and testable without sleeps. After [`MAX_FAILED_UNLOCK_ATTEMPTS`]
//! consecutive failures a finite lockout window opens; once it elapses the
//! throttle self-resets and permits a fresh attempt.

use chrono::{DateTime, Duration, Utc};

/// Consecutive failed unlocks that trigger a lockout window.
pub(crate) const MAX_FAILED_UNLOCK_ATTEMPTS: u32 = 5;
/// Length of the finite lockout window, in seconds.
pub(crate) const FAILED_UNLOCK_LOCKOUT_SECONDS: i64 = 30;

#[derive(Clone, Debug, Default)]
pub(crate) struct UnlockThrottle {
    failed_attempts: u32,
    retry_at: Option<DateTime<Utc>>,
}

impl UnlockThrottle {
    pub(crate) const fn failed_attempts(&self) -> u32 {
        self.failed_attempts
    }

    pub(crate) const fn retry_at(&self) -> Option<DateTime<Utc>> {
        self.retry_at
    }

    /// Gate an unlock attempt at `now`. Returns `Err(retry_at)` while still
    /// rate-limited. Once the window has elapsed the throttle resets and returns
    /// `Ok(())`, so the caller may proceed to key derivation.
    pub(crate) fn check(&mut self, now: DateTime<Utc>) -> Result<(), DateTime<Utc>> {
        if let Some(retry_at) = self.retry_at {
            if now < retry_at {
                return Err(retry_at);
            }
            self.reset();
        }
        Ok(())
    }

    /// Record one failed attempt at `now`, opening the lockout window on the
    /// threshold failure.
    pub(crate) fn record_failure(&mut self, now: DateTime<Utc>) {
        self.failed_attempts = self.failed_attempts.saturating_add(1);
        if self.failed_attempts >= MAX_FAILED_UNLOCK_ATTEMPTS {
            self.retry_at = Some(now + Duration::seconds(FAILED_UNLOCK_LOCKOUT_SECONDS));
        }
    }

    pub(crate) fn reset(&mut self) {
        self.failed_attempts = 0;
        self.retry_at = None;
    }
}

#[cfg(test)]
mod tests {
    use chrono::TimeZone;

    use super::*;

    fn now() -> DateTime<Utc> {
        Utc.with_ymd_and_hms(2026, 7, 22, 0, 0, 0).unwrap()
    }

    #[test]
    fn vault_state_throttle_opens_finite_window_on_threshold() {
        let mut throttle = UnlockThrottle::default();
        for _ in 0..MAX_FAILED_UNLOCK_ATTEMPTS {
            assert!(throttle.check(now()).is_ok());
            throttle.record_failure(now());
        }
        let retry_at = throttle.check(now()).expect_err("must be locked out");
        assert_eq!(retry_at, throttle.retry_at().unwrap());

        let after = retry_at + Duration::seconds(1);
        assert!(throttle.check(after).is_ok(), "window is finite");
        assert_eq!(throttle.failed_attempts(), 0);
    }
}
