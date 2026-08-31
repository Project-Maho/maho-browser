-- Migration: 0024_create_release_updates.sql

CREATE TABLE release_updates (
    platform          TEXT    NOT NULL,
    channel           TEXT    NOT NULL,
    version           TEXT    NOT NULL,
    version_code      INTEGER,
    rollout_threshold INTEGER NOT NULL DEFAULT 100,
    appcast_xml       TEXT,
    win_envelope      TEXT,
    notes_url         TEXT,
    pub_date          TEXT,
    created_at        INTEGER,
    PRIMARY KEY (platform, channel, version)
);

CREATE INDEX idx_release_updates_platform_channel_version_code
    ON release_updates (platform, channel, version_code DESC);
