//! D1-backed persistence for resumable handoff jobs (plan task 18).
//!
//! Thin async wrappers around the `handoff_jobs` table. All state-transition
//! logic lives in the pure `crate::jobs` state machine; this layer only reads
//! and writes durable rows. D1-facing rows use primitive column types
//! (`status` as text, `retryable` as 0/1) and convert to the domain
//! `JobRecord` to avoid JS-number/bool deserialization pitfalls.

use crate::errors::{AppError, Result};
use crate::jobs::{JobRecord, JobStatus};
use serde::Deserialize;
use worker::*;

use super::js;

#[derive(Debug, Deserialize)]
struct JobRow {
    id: String,
    user_id: i32,
    job_type: String,
    status: String,
    created_at: i64,
    updated_at: i64,
    steps_completed: i64,
    estimated_cost_usd: f64,
    billed_usd: f64,
    billed_at: Option<i64>,
    result_ref: Option<String>,
    error: Option<String>,
    retryable: i64,
}

impl JobRow {
    fn into_record(self) -> Result<JobRecord> {
        let status = JobStatus::parse(&self.status)
            .ok_or_else(|| AppError::Internal(format!("bad job status: {}", self.status)))?;
        Ok(JobRecord {
            id: self.id,
            user_id: self.user_id,
            job_type: self.job_type,
            status,
            created_at: self.created_at,
            updated_at: self.updated_at,
            steps_completed: self.steps_completed.max(0) as u32,
            estimated_cost_usd: self.estimated_cost_usd,
            billed_usd: self.billed_usd,
            billed_at: self.billed_at,
            result_ref: self.result_ref,
            error: self.error,
            retryable: self.retryable != 0,
        })
    }
}

pub async fn get_job(db: &D1Database, id: &str) -> Result<Option<JobRecord>> {
    let stmt = db.prepare("SELECT * FROM handoff_jobs WHERE id = ?1");
    let stmt = stmt.bind(&[js(&id)?])?;
    match stmt.first::<JobRow>(None).await? {
        Some(row) => Ok(Some(row.into_record()?)),
        None => Ok(None),
    }
}

/// Idempotent create: inserts the pending job if `id` is new, then returns the
/// stored record (existing one on conflict). Never overwrites.
pub async fn insert_job_if_absent(db: &D1Database, record: &JobRecord) -> Result<JobRecord> {
    let stmt = db.prepare(
        "INSERT INTO handoff_jobs (id, user_id, job_type, status, created_at, updated_at, steps_completed, estimated_cost_usd, billed_usd, retryable)
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, 0.0, 0)
         ON CONFLICT(id) DO NOTHING",
    );
    let stmt = stmt.bind(&[
        js(&record.id)?,
        js(&record.user_id)?,
        js(&record.job_type)?,
        js(&record.status.as_str())?,
        js(&record.created_at)?,
        js(&record.updated_at)?,
        js(&(record.steps_completed as i64))?,
        js(&record.estimated_cost_usd)?,
    ])?;
    stmt.run().await?;
    get_job(db, &record.id)
        .await?
        .ok_or_else(|| AppError::Internal("job vanished after insert".to_string()))
}

/// Atomically complete-and-bill a job exactly once.
///
/// The `billed_at IS NULL AND status IN ('pending','running')` precondition is
/// the DB-level enforcement of the pure state machine's bill-once guarantee:
/// concurrent completions race on this single conditional UPDATE, so only one
/// wins and returns a row. `None` means the job was already billed or is not in
/// a completable state — the caller reads the current row to disambiguate.
pub async fn try_complete_job(
    db: &D1Database,
    id: &str,
    bill_usd: f64,
    result_ref: Option<String>,
    now: i64,
) -> Result<Option<JobRecord>> {
    let stmt = db.prepare(
        "UPDATE handoff_jobs
            SET status='completed', billed_usd=?2, billed_at=?3, result_ref=?4, updated_at=?3
          WHERE id=?1 AND billed_at IS NULL AND status IN ('pending','running')
         RETURNING *",
    );
    let stmt = stmt.bind(&[js(&id)?, js(&bill_usd)?, js(&now)?, js(&result_ref)?])?;
    match stmt.first::<JobRow>(None).await? {
        Some(row) => Ok(Some(row.into_record()?)),
        None => Ok(None),
    }
}

/// Atomically fail a non-terminal job. The `status IN ('pending','running')`
/// precondition guarantees a completed job is never reverted to failed at the
/// DB level. `None` means the job was terminal (completed or failed already).
pub async fn try_fail_job(
    db: &D1Database,
    id: &str,
    error: &str,
    retryable: bool,
    now: i64,
) -> Result<Option<JobRecord>> {
    let retryable_int: i64 = if retryable { 1 } else { 0 };
    let stmt = db.prepare(
        "UPDATE handoff_jobs
            SET status='failed', error=?2, retryable=?3, updated_at=?4
          WHERE id=?1 AND status IN ('pending','running')
         RETURNING *",
    );
    let stmt = stmt.bind(&[js(&id)?, js(&error)?, js(&retryable_int)?, js(&now)?])?;
    match stmt.first::<JobRow>(None).await? {
        Some(row) => Ok(Some(row.into_record()?)),
        None => Ok(None),
    }
}

/// Advance a non-terminal job to `running` with a monotonically non-decreasing
/// `steps_completed`. Terminal jobs and stale (lower) progress reports are
/// no-ops (`None`), so a retried report cannot rewind progress.
pub async fn try_progress_job(
    db: &D1Database,
    id: &str,
    steps: u32,
    now: i64,
) -> Result<Option<JobRecord>> {
    let stmt = db.prepare(
        "UPDATE handoff_jobs
            SET status='running', steps_completed=?2, updated_at=?3
          WHERE id=?1 AND status IN ('pending','running') AND steps_completed <= ?2
         RETURNING *",
    );
    let stmt = stmt.bind(&[js(&id)?, js(&(steps as i64))?, js(&now)?])?;
    match stmt.first::<JobRow>(None).await? {
        Some(row) => Ok(Some(row.into_record()?)),
        None => Ok(None),
    }
}
