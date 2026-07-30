-- Migration: 0017_config_updates_composite_pk.sql
--
-- The PK was `version` alone while `channel` is a separate column, so
-- INSERT OR REPLACE of e.g. beta v7 silently overwrote stable v7. Recreate the
-- table with a composite (channel, version) primary key. SQLite cannot alter a
-- primary key in place, hence the create/copy/drop/rename dance. The copy is
-- safe: under the old single-column PK, version was globally unique, so no
-- duplicate (channel, version) pair can exist.
--
-- Rollback (manual): recreate with `version INTEGER PRIMARY KEY`, copy back,
-- drop, rename.

CREATE TABLE config_updates_new (
    version           INTEGER NOT NULL,
    payload           TEXT    NOT NULL,
    signature         TEXT    NOT NULL,
    rollout_threshold INTEGER NOT NULL DEFAULT 100,
    channel           TEXT    NOT NULL DEFAULT 'stable',
    PRIMARY KEY (channel, version)
);

INSERT INTO config_updates_new (version, payload, signature, rollout_threshold, channel)
    SELECT version, payload, signature, rollout_threshold, channel FROM config_updates;

DROP TABLE config_updates;

ALTER TABLE config_updates_new RENAME TO config_updates;

CREATE INDEX idx_config_updates_channel_version ON config_updates (channel, version DESC);
