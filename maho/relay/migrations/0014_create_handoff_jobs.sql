-- Migration: 0014_create_handoff_jobs.sql
--
-- Durable, metadata-only state for resumable managed cloud-handoff jobs
-- (plan task 18). NO prompt or response text is stored here: `result_ref` is an
-- opaque pointer to a user-visible / client-held (E2EE) artifact. `billed_usd`
-- and `billed_at` form the bill-once idempotency marker so a retried completion
-- never double-charges. Remote Worker execution of the job body is DEFERRED;
-- this table backs create/poll/complete/fail state only. See ADR 0015.

CREATE TABLE handoff_jobs (
    id TEXT NOT NULL PRIMARY KEY,
    user_id INTEGER NOT NULL,
    job_type TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'pending',
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    steps_completed INTEGER NOT NULL DEFAULT 0,
    estimated_cost_usd REAL NOT NULL DEFAULT 0.0,
    billed_usd REAL NOT NULL DEFAULT 0.0,
    billed_at INTEGER,
    result_ref TEXT,
    error TEXT,
    retryable INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY (user_id) REFERENCES users (id)
);

CREATE INDEX idx_handoff_jobs_user ON handoff_jobs(user_id, updated_at);
