//! Resumable managed cloud-handoff job state (plan task 18).
//!
//! This module owns the pure, D1-independent state machine for long-running
//! managed AI jobs (e.g. a bounded deep-research run) so a client that
//! disconnects can reconnect and poll a durable job by id. It is metadata-only:
//! NO prompt or response text is stored here. `result_ref` is an opaque pointer
//! to a user-visible / client-held artifact, never inline content.
//!
//! SCOPE (honest): this pass implements durable job STATE with
//! create/poll/complete/fail semantics and idempotent (bill-once) accounting,
//! backed by D1 in production (see `db/jobs.rs`). Remote Worker EXECUTION of the
//! job body is DEFERRED — nothing in the Worker runs the job yet; a local/CLI
//! producer drives the work and reports completion. See ADR 0015.
//!
//! The D1-backed routes (async) wrap these pure decisions, mirroring how
//! `billing::decide_credit_consume` is a pure function wrapped by the
//! `consume_credit` route. Tests exercise the machine through the in-memory
//! `MemoryJobStore` with no Cloudflare runtime.

use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum JobStatus {
    Pending,
    Running,
    Completed,
    Failed,
}

impl JobStatus {
    pub fn as_str(&self) -> &'static str {
        match self {
            JobStatus::Pending => "pending",
            JobStatus::Running => "running",
            JobStatus::Completed => "completed",
            JobStatus::Failed => "failed",
        }
    }

    pub fn parse(s: &str) -> Option<Self> {
        match s {
            "pending" => Some(JobStatus::Pending),
            "running" => Some(JobStatus::Running),
            "completed" => Some(JobStatus::Completed),
            "failed" => Some(JobStatus::Failed),
            _ => None,
        }
    }

    pub fn is_terminal(&self) -> bool {
        matches!(self, JobStatus::Completed | JobStatus::Failed)
    }
}

/// Durable, metadata-only job record. Contains no prompt/response text.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct JobRecord {
    pub id: String,
    pub user_id: i32,
    pub job_type: String,
    pub status: JobStatus,
    pub created_at: i64,
    pub updated_at: i64,
    pub steps_completed: u32,
    pub estimated_cost_usd: f64,
    /// Amount actually billed. Non-zero only after the first completion.
    pub billed_usd: f64,
    /// Idempotency marker: set once, on the first billed completion.
    pub billed_at: Option<i64>,
    /// Opaque reference to a user-visible artifact (e.g. a client-held/E2EE id).
    pub result_ref: Option<String>,
    pub error: Option<String>,
    pub retryable: bool,
}

impl JobRecord {
    pub fn new(
        id: String,
        user_id: i32,
        job_type: String,
        estimated_cost_usd: f64,
        now: i64,
    ) -> Self {
        Self {
            id,
            user_id,
            job_type,
            status: JobStatus::Pending,
            created_at: now,
            updated_at: now,
            steps_completed: 0,
            estimated_cost_usd,
            billed_usd: 0.0,
            billed_at: None,
            result_ref: None,
            error: None,
            retryable: false,
        }
    }
}

/// Outcome of a create request. Create is idempotent on `id`: re-creating an
/// existing job returns the stored record rather than overwriting it.
#[derive(Debug, PartialEq)]
pub enum CreateOutcome {
    Created(JobRecord),
    AlreadyExists(JobRecord),
}

/// Outcome of a complete request.
#[derive(Debug, PartialEq)]
pub enum CompleteOutcome {
    /// First completion: `bill_usd` MUST be charged exactly once by the caller.
    Billed { record: JobRecord, bill_usd: f64 },
    /// Idempotent replay of a completed job: do NOT bill again.
    AlreadyBilled { record: JobRecord },
    /// Cannot complete from the current state (e.g. already failed).
    InvalidState { current: JobStatus },
}

/// Outcome of a fail request.
#[derive(Debug, PartialEq)]
pub enum FailOutcome {
    Failed(JobRecord),
    /// Job already completed; failing it is rejected (never un-bill/un-complete).
    AlreadyCompleted(JobRecord),
    /// Idempotent replay of an already-failed job.
    AlreadyFailed(JobRecord),
}

/// The amount to bill for a completion: the reported cost when finite and
/// positive, otherwise zero. Shared by the pure state machine and the atomic
/// D1 completion path so both agree on the charge.
pub fn bill_amount(final_cost_usd: f64) -> f64 {
    if final_cost_usd.is_finite() && final_cost_usd > 0.0 {
        final_cost_usd
    } else {
        0.0
    }
}

/// Decide the completion transition for `job`, charging `final_cost_usd` at most
/// once. Pure: returns the new record without persisting it.
pub fn decide_complete(
    job: &JobRecord,
    final_cost_usd: f64,
    result_ref: Option<String>,
    now: i64,
) -> CompleteOutcome {
    match job.status {
        JobStatus::Completed => CompleteOutcome::AlreadyBilled {
            record: job.clone(),
        },
        JobStatus::Failed => CompleteOutcome::InvalidState {
            current: JobStatus::Failed,
        },
        JobStatus::Pending | JobStatus::Running => {
            let bill_usd = bill_amount(final_cost_usd);
            let mut record = job.clone();
            record.status = JobStatus::Completed;
            record.billed_usd = bill_usd;
            record.billed_at = Some(now);
            record.result_ref = result_ref;
            record.updated_at = now;
            CompleteOutcome::Billed { record, bill_usd }
        }
    }
}

/// Decide the failure transition for `job`. A completed job is never failed
/// (its bill stands); a failed job is returned idempotently.
pub fn decide_fail(job: &JobRecord, error: String, retryable: bool, now: i64) -> FailOutcome {
    match job.status {
        JobStatus::Completed => FailOutcome::AlreadyCompleted(job.clone()),
        JobStatus::Failed => FailOutcome::AlreadyFailed(job.clone()),
        JobStatus::Pending | JobStatus::Running => {
            let mut record = job.clone();
            record.status = JobStatus::Failed;
            record.error = Some(error);
            record.retryable = retryable;
            record.updated_at = now;
            FailOutcome::Failed(record)
        }
    }
}

/// Advance a job to `Running` and record progress. Terminal jobs are unchanged.
pub fn decide_progress(job: &JobRecord, steps_completed: u32, now: i64) -> Option<JobRecord> {
    if job.status.is_terminal() {
        return None;
    }
    let mut record = job.clone();
    record.status = JobStatus::Running;
    record.steps_completed = steps_completed;
    record.updated_at = now;
    Some(record)
}

/// A minimal, synchronous, in-memory job store used by tests to exercise the
/// full create/poll/complete/fail lifecycle without any Cloudflare runtime.
/// Production uses the D1-backed store in `db/jobs.rs`.
#[derive(Default)]
pub struct MemoryJobStore {
    jobs: HashMap<String, JobRecord>,
}

impl MemoryJobStore {
    pub fn new() -> Self {
        Self {
            jobs: HashMap::new(),
        }
    }

    pub fn create(
        &mut self,
        id: &str,
        user_id: i32,
        job_type: &str,
        estimated_cost_usd: f64,
        now: i64,
    ) -> CreateOutcome {
        if let Some(existing) = self.jobs.get(id) {
            return CreateOutcome::AlreadyExists(existing.clone());
        }
        let record = JobRecord::new(
            id.to_string(),
            user_id,
            job_type.to_string(),
            estimated_cost_usd,
            now,
        );
        self.jobs.insert(id.to_string(), record.clone());
        CreateOutcome::Created(record)
    }

    pub fn get(&self, id: &str) -> Option<JobRecord> {
        self.jobs.get(id).cloned()
    }

    pub fn complete(
        &mut self,
        id: &str,
        final_cost_usd: f64,
        result_ref: Option<String>,
        now: i64,
    ) -> Option<CompleteOutcome> {
        let job = self.jobs.get(id)?.clone();
        let outcome = decide_complete(&job, final_cost_usd, result_ref, now);
        if let CompleteOutcome::Billed { record, .. } = &outcome {
            self.jobs.insert(id.to_string(), record.clone());
        }
        Some(outcome)
    }

    pub fn fail(
        &mut self,
        id: &str,
        error: &str,
        retryable: bool,
        now: i64,
    ) -> Option<FailOutcome> {
        let job = self.jobs.get(id)?.clone();
        let outcome = decide_fail(&job, error.to_string(), retryable, now);
        if let FailOutcome::Failed(record) = &outcome {
            self.jobs.insert(id.to_string(), record.clone());
        }
        Some(outcome)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn store_with_job() -> (MemoryJobStore, String) {
        let mut store = MemoryJobStore::new();
        let id = "job-abc".to_string();
        let created = store.create(&id, 7, "deep_research", 0.50, 1_000);
        assert!(matches!(created, CreateOutcome::Created(_)));
        (store, id)
    }

    #[test]
    fn create_is_idempotent_on_id() {
        let (mut store, id) = store_with_job();
        let again = store.create(&id, 7, "deep_research", 0.50, 2_000);
        match again {
            CreateOutcome::AlreadyExists(r) => {
                assert_eq!(r.created_at, 1_000, "must not overwrite the original");
                assert_eq!(r.status, JobStatus::Pending);
            }
            CreateOutcome::Created(_) => panic!("second create must not create a new job"),
        }
    }

    #[test]
    fn poll_after_disconnect_returns_persisted_state() {
        let (mut store, id) = store_with_job();
        // Simulate progress before a client "disconnect".
        let progressed = decide_progress(&store.get(&id).unwrap(), 3, 1_500).unwrap();
        store.jobs.insert(id.clone(), progressed);

        // A brand-new poll (reconnect) sees the durable state.
        let polled = store
            .get(&id)
            .expect("job must still exist after disconnect");
        assert_eq!(polled.status, JobStatus::Running);
        assert_eq!(polled.steps_completed, 3);
    }

    #[test]
    fn complete_bills_exactly_once() {
        let (mut store, id) = store_with_job();

        let first = store
            .complete(&id, 0.42, Some("artifact-1".into()), 2_000)
            .unwrap();
        match first {
            CompleteOutcome::Billed { record, bill_usd } => {
                assert_eq!(bill_usd, 0.42);
                assert_eq!(record.status, JobStatus::Completed);
                assert_eq!(record.billed_usd, 0.42);
                assert_eq!(record.billed_at, Some(2_000));
                assert_eq!(record.result_ref.as_deref(), Some("artifact-1"));
            }
            other => panic!("first completion must bill: {other:?}"),
        }

        // Replayed completion (e.g. retried network call) must NOT bill again.
        let replay = store
            .complete(&id, 0.42, Some("artifact-1".into()), 3_000)
            .unwrap();
        match replay {
            CompleteOutcome::AlreadyBilled { record } => {
                assert_eq!(record.billed_usd, 0.42, "balance must not double-charge");
                assert_eq!(
                    record.billed_at,
                    Some(2_000),
                    "original bill timestamp stands"
                );
            }
            other => panic!("replay must be idempotent: {other:?}"),
        }
    }

    #[test]
    fn failed_job_is_retryable_and_not_billed() {
        let (mut store, id) = store_with_job();
        let outcome = store.fail(&id, "worker crashed", true, 2_000).unwrap();
        match outcome {
            FailOutcome::Failed(record) => {
                assert_eq!(record.status, JobStatus::Failed);
                assert!(record.retryable);
                assert_eq!(record.billed_usd, 0.0, "failed jobs are never billed");
                assert_eq!(record.error.as_deref(), Some("worker crashed"));
            }
            other => panic!("expected fail: {other:?}"),
        }
    }

    #[test]
    fn cannot_complete_a_failed_job() {
        let (mut store, id) = store_with_job();
        store.fail(&id, "boom", false, 2_000).unwrap();
        let outcome = store.complete(&id, 0.42, None, 3_000).unwrap();
        assert_eq!(
            outcome,
            CompleteOutcome::InvalidState {
                current: JobStatus::Failed
            }
        );
        // And the stored record stays failed and unbilled.
        let record = store.get(&id).unwrap();
        assert_eq!(record.status, JobStatus::Failed);
        assert_eq!(record.billed_usd, 0.0);
    }

    #[test]
    fn cannot_fail_a_completed_job() {
        let (mut store, id) = store_with_job();
        store.complete(&id, 0.10, None, 2_000).unwrap();
        let outcome = store.fail(&id, "late failure", true, 3_000).unwrap();
        match outcome {
            FailOutcome::AlreadyCompleted(record) => {
                assert_eq!(record.status, JobStatus::Completed);
                assert_eq!(record.billed_usd, 0.10, "completed bill must stand");
            }
            other => panic!("must not un-complete: {other:?}"),
        }
    }

    #[test]
    fn double_fail_is_idempotent() {
        let (mut store, id) = store_with_job();
        store.fail(&id, "first", true, 2_000).unwrap();
        let second = store.fail(&id, "second", false, 3_000).unwrap();
        match second {
            FailOutcome::AlreadyFailed(record) => {
                assert_eq!(
                    record.error.as_deref(),
                    Some("first"),
                    "first failure reason stands"
                );
                assert!(record.retryable);
            }
            other => panic!("expected idempotent fail: {other:?}"),
        }
    }

    #[test]
    fn zero_or_negative_final_cost_bills_zero() {
        let (mut store, id) = store_with_job();
        let outcome = store.complete(&id, -5.0, None, 2_000).unwrap();
        match outcome {
            CompleteOutcome::Billed { bill_usd, .. } => assert_eq!(bill_usd, 0.0),
            other => panic!("expected billed-zero: {other:?}"),
        }
    }

    #[test]
    fn poll_missing_job_returns_none() {
        let store = MemoryJobStore::new();
        assert!(store.get("nope").is_none());
    }

    #[test]
    fn job_status_round_trips() {
        for s in [
            JobStatus::Pending,
            JobStatus::Running,
            JobStatus::Completed,
            JobStatus::Failed,
        ] {
            assert_eq!(JobStatus::parse(s.as_str()), Some(s));
        }
        assert_eq!(JobStatus::parse("bogus"), None);
    }
}
