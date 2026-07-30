//! Service-token-gated HTTP handlers for resumable handoff jobs (plan task 18).
//!
//! These Worker routes wrap the pure `crate::jobs` state machine and the
//! `crate::db::jobs` D1 store. They are service-token only (Worker-to-relay),
//! matching the `consume_credit` contract. The job record is metadata-only; the
//! `billed_usd`/`billed_at` marker is the bill-once idempotency source of truth
//! so the caller (billing-proxy) debits credits exactly once per completion.

use crate::db;
use crate::errors::{AppError, Result};
use crate::jobs::{bill_amount, JobRecord, JobStatus};
use serde::{Deserialize, Serialize};
use worker::*;

use crate::auth::routes::require_service_token;

#[derive(Debug, Deserialize)]
pub struct CreateJobRequest {
    pub id: String,
    pub user_id: i32,
    pub job_type: String,
    #[serde(default)]
    pub estimated_cost_usd: f64,
}

pub async fn create_job(mut req: Request, env: Env) -> Result<Response> {
    require_service_token(&req, &env)?;
    let payload = req.json::<CreateJobRequest>().await?;
    if payload.id.trim().is_empty() {
        return Err(AppError::BadRequest("job id required".to_string()));
    }
    let db = env.d1("MAHO_DB")?;
    let now = chrono::Utc::now().timestamp();
    let record = JobRecord::new(
        payload.id,
        payload.user_id,
        payload.job_type,
        payload.estimated_cost_usd,
        now,
    );
    let stored = db::jobs::insert_job_if_absent(&db, &record).await?;
    Ok(Response::from_json(&stored)?)
}

pub async fn get_job(req: Request, env: Env, id: &str) -> Result<Response> {
    require_service_token(&req, &env)?;
    let db = env.d1("MAHO_DB")?;
    match db::jobs::get_job(&db, id).await? {
        Some(record) => Ok(Response::from_json(&record)?),
        None => Err(AppError::NotFound("job not found".to_string())),
    }
}

#[derive(Debug, Deserialize)]
pub struct CompleteJobRequest {
    pub id: String,
    #[serde(default)]
    pub final_cost_usd: f64,
    #[serde(default)]
    pub result_ref: Option<String>,
}

#[derive(Debug, Serialize)]
pub struct CompleteJobResponse {
    pub billed: bool,
    pub bill_usd: f64,
    pub job: JobRecord,
}

pub async fn complete_job(mut req: Request, env: Env) -> Result<Response> {
    require_service_token(&req, &env)?;
    let payload = req.json::<CompleteJobRequest>().await?;
    let db = env.d1("MAHO_DB")?;
    let now = chrono::Utc::now().timestamp();

    let bill_usd = bill_amount(payload.final_cost_usd);

    if let Some(record) =
        db::jobs::try_complete_job(&db, &payload.id, bill_usd, payload.result_ref, now).await?
    {
        return Ok(Response::from_json(&CompleteJobResponse {
            billed: true,
            bill_usd,
            job: record,
        })?);
    }

    // Zero rows updated: job is terminal or absent. Read it back to answer
    // idempotently — completed is a benign replay, failed is a client error.
    let job = db::jobs::get_job(&db, &payload.id)
        .await?
        .ok_or_else(|| AppError::NotFound("job not found".to_string()))?;
    match job.status {
        JobStatus::Completed => Ok(Response::from_json(&CompleteJobResponse {
            billed: false,
            bill_usd: 0.0,
            job,
        })?),
        current => Err(AppError::BadRequest(format!(
            "cannot complete a job in state {}",
            current.as_str()
        ))),
    }
}

#[derive(Debug, Deserialize)]
pub struct ProgressJobRequest {
    pub id: String,
    pub steps_completed: u32,
}

pub async fn progress_job(mut req: Request, env: Env) -> Result<Response> {
    require_service_token(&req, &env)?;
    let payload = req.json::<ProgressJobRequest>().await?;
    let db = env.d1("MAHO_DB")?;
    let now = chrono::Utc::now().timestamp();

    if let Some(record) =
        db::jobs::try_progress_job(&db, &payload.id, payload.steps_completed, now).await?
    {
        return Ok(Response::from_json(&record)?);
    }

    // No-op: the job is terminal or the report is stale. Return the current
    // record unchanged so the caller sees the real status and steps_completed.
    let job = db::jobs::get_job(&db, &payload.id)
        .await?
        .ok_or_else(|| AppError::NotFound("job not found".to_string()))?;
    Ok(Response::from_json(&job)?)
}

#[derive(Debug, Deserialize)]
pub struct FailJobRequest {
    pub id: String,
    pub error: String,
    #[serde(default)]
    pub retryable: bool,
}

pub async fn fail_job(mut req: Request, env: Env) -> Result<Response> {
    require_service_token(&req, &env)?;
    let payload = req.json::<FailJobRequest>().await?;
    let db = env.d1("MAHO_DB")?;
    let now = chrono::Utc::now().timestamp();

    if let Some(record) =
        db::jobs::try_fail_job(&db, &payload.id, &payload.error, payload.retryable, now).await?
    {
        return Ok(Response::from_json(&record)?);
    }

    let job = db::jobs::get_job(&db, &payload.id)
        .await?
        .ok_or_else(|| AppError::NotFound("job not found".to_string()))?;
    match job.status {
        JobStatus::Failed => Ok(Response::from_json(&job)?),
        JobStatus::Completed => Err(AppError::BadRequest(format!(
            "cannot fail job {}: already completed",
            job.id
        ))),
        current => Err(AppError::BadRequest(format!(
            "cannot fail a job in state {}",
            current.as_str()
        ))),
    }
}
