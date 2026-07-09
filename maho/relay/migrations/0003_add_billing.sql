-- Migration: 0003_add_billing.sql

ALTER TABLE users ADD COLUMN tier TEXT NOT NULL DEFAULT 'free';
ALTER TABLE users ADD COLUMN subscription_status TEXT NOT NULL DEFAULT 'none';
ALTER TABLE users ADD COLUMN email_verified BOOLEAN NOT NULL DEFAULT 0;
ALTER TABLE users ADD COLUMN subscription_expires_at INTEGER;
ALTER TABLE users ADD COLUMN period_start_at INTEGER;
ALTER TABLE users ADD COLUMN anniversary_reset_day INTEGER;

CREATE TABLE processed_events (
    event_id TEXT NOT NULL PRIMARY KEY,
    processed_at INTEGER NOT NULL
);
