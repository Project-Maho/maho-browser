-- Migration: 0012_create_config_updates.sql

CREATE TABLE config_updates (
    version           INTEGER NOT NULL PRIMARY KEY,
    payload           TEXT    NOT NULL,
    signature         TEXT    NOT NULL,
    rollout_threshold INTEGER NOT NULL DEFAULT 100,
    channel           TEXT    NOT NULL DEFAULT 'stable'
);

CREATE INDEX idx_config_updates_channel_version ON config_updates (channel, version DESC);
