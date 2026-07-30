-- Migration: 0015_create_admin_audit.sql
--
-- Durable audit trail for admin mutations (set_credit_balance,
-- force_verify_email). The `reason` on these requests was previously parsed and
-- discarded; it is now persisted here. Never store PII (email bodies, tokens,
-- prompts) in `detail` — only before/after values and identifiers.

CREATE TABLE admin_audit (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    action TEXT NOT NULL,          -- force_verify_email | set_credit_balance
    target_user_id INTEGER NOT NULL,
    reason TEXT NOT NULL,
    detail TEXT,                   -- before/after values, no PII
    created_at INTEGER NOT NULL
);

CREATE INDEX idx_admin_audit_target ON admin_audit (target_user_id, created_at);
CREATE INDEX idx_admin_audit_created_at ON admin_audit (created_at);
