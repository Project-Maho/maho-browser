-- Migration: 0001_create_tables.sql

CREATE TABLE messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    room_id TEXT NOT NULL,
    seq INTEGER NOT NULL,
    blob BLOB NOT NULL,
    created_at INTEGER NOT NULL
);

CREATE UNIQUE INDEX idx_messages_room_seq ON messages (room_id, seq);

CREATE TABLE room_meta (
    room_id TEXT NOT NULL PRIMARY KEY,
    next_seq INTEGER NOT NULL DEFAULT 0,
    last_activity INTEGER NOT NULL,
    total_bytes INTEGER NOT NULL DEFAULT 0
);
