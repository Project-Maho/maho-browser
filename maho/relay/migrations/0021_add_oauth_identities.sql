-- Migration: 0021_add_oauth_identities.sql

-- OAuth-only accounts store password_hash = '' (the column is NOT NULL and a D1
-- table rewrite is riskier than a sentinel); login() rejects an empty stored
-- hash before Argon2, so no password can authenticate such an account.
CREATE TABLE oauth_identities (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL,
    provider TEXT NOT NULL,
    provider_sub TEXT NOT NULL,
    email TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    last_login_at INTEGER,
    FOREIGN KEY (user_id) REFERENCES users (id) ON DELETE CASCADE ON UPDATE CASCADE
);

CREATE UNIQUE INDEX idx_oauth_identities_provider_sub
    ON oauth_identities (provider, provider_sub);
CREATE INDEX idx_oauth_identities_user_id ON oauth_identities (user_id);

-- Replay defense: a captured id_token stays signature-valid until exp, so the
-- nonce must be single-use.
CREATE TABLE consumed_oauth_nonces (
    nonce_hash BLOB NOT NULL PRIMARY KEY,
    consumed_at INTEGER NOT NULL
);

CREATE INDEX idx_consumed_oauth_nonces_consumed_at
    ON consumed_oauth_nonces (consumed_at);
