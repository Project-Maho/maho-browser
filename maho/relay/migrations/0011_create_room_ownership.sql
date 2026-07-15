-- Migration: 0011_create_room_ownership.sql

CREATE TABLE room_ownership (
    room_id    TEXT    NOT NULL PRIMARY KEY,
    user_id    INTEGER NOT NULL,
    claimed_at INTEGER NOT NULL
);

CREATE INDEX idx_room_ownership_user ON room_ownership (user_id);
