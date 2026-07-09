-- Migration: 0004_add_email_verification.sql

ALTER TABLE users ADD COLUMN verification_token TEXT;
ALTER TABLE users ADD COLUMN verification_token_expires_at INTEGER;
