-- Migration: 0008_add_waitlist_signups.sql

CREATE TABLE waitlist_signups (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    email TEXT NOT NULL,
    source TEXT NOT NULL DEFAULT 'general',
    ip_hash TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    metadata TEXT
);

CREATE UNIQUE INDEX idx_waitlist_email_source ON waitlist_signups (email, source);
CREATE INDEX idx_waitlist_created_at ON waitlist_signups (created_at);
