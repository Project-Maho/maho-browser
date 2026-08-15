-- Migration: 0023_create_user_sync_bootstrap.sql
--
-- The account-owned Sync bootstrap seed is encrypted by the Relay before it
-- reaches D1. The canonical room is deterministic from that seed and lets an
-- authenticated client recover exactly one account-scoped Sync configuration.

CREATE TABLE user_sync_bootstrap (
    user_id    INTEGER NOT NULL PRIMARY KEY,
    room_id    TEXT    NOT NULL UNIQUE,
    version    INTEGER NOT NULL,
    ciphertext BLOB    NOT NULL,
    nonce      BLOB    NOT NULL,
    created_at INTEGER NOT NULL,
    FOREIGN KEY (user_id) REFERENCES users (id) ON DELETE CASCADE ON UPDATE CASCADE
);
