-- Migration: 0002_create_auth_tables.sql

CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    email TEXT NOT NULL,
    password_hash TEXT NOT NULL,
    display_name TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    locked_until INTEGER,
    failed_login_attempts INTEGER NOT NULL DEFAULT 0,
    last_failed_login_at INTEGER
);

CREATE UNIQUE INDEX idx_users_email ON users (email);

CREATE TABLE devices (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL,
    name TEXT NOT NULL,
    device_type TEXT NOT NULL,
    room_id TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    last_seen INTEGER,
    FOREIGN KEY (user_id) REFERENCES users (id) ON DELETE CASCADE ON UPDATE CASCADE
);

CREATE INDEX idx_devices_user_id ON devices (user_id);
CREATE UNIQUE INDEX idx_devices_room_id ON devices (room_id);

CREATE TABLE sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL,
    device_id INTEGER NOT NULL,
    token_hash BLOB NOT NULL,
    refresh_hash BLOB NOT NULL,
    expires_at INTEGER NOT NULL,
    refresh_expires_at INTEGER NOT NULL,
    last_used_at INTEGER,
    created_at INTEGER NOT NULL,
    FOREIGN KEY (user_id) REFERENCES users (id) ON DELETE CASCADE ON UPDATE CASCADE,
    FOREIGN KEY (device_id) REFERENCES devices (id) ON DELETE CASCADE ON UPDATE CASCADE
);

CREATE INDEX idx_sessions_user_id ON sessions (user_id);
CREATE INDEX idx_sessions_device_id ON sessions (device_id);
CREATE UNIQUE INDEX idx_sessions_token_hash ON sessions (token_hash);
CREATE UNIQUE INDEX idx_sessions_refresh_hash ON sessions (refresh_hash);
