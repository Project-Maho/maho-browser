-- Migration: 0005_add_consumed_bypass_tokens.sql

CREATE TABLE consumed_bypass_tokens (
    token_hash BLOB NOT NULL PRIMARY KEY,
    user_id INTEGER NOT NULL,
    consumed_at INTEGER NOT NULL
);
