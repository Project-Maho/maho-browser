-- Migration: 0010_create_snapshots.sql

CREATE TABLE snapshots (
    room_id TEXT NOT NULL PRIMARY KEY,
    hlc_ts_ceiling INTEGER NOT NULL,
    created_at INTEGER NOT NULL,
    creator_device_id INTEGER NOT NULL,
    blob BLOB NOT NULL
);
