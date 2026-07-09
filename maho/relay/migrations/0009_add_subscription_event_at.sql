-- Migration: 0009_add_subscription_event_at.sql

ALTER TABLE users ADD COLUMN subscription_event_at INTEGER;
