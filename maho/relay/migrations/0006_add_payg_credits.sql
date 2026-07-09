-- Migration: 0006_add_payg_credits.sql

CREATE TABLE credits (
    user_id INTEGER NOT NULL PRIMARY KEY,
    balance_usd REAL NOT NULL DEFAULT 0.0,
    lifetime_purchased_usd REAL NOT NULL DEFAULT 0.0,
    last_purchase_at INTEGER,
    last_consumption_at INTEGER,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    FOREIGN KEY (user_id) REFERENCES users (id)
);

CREATE INDEX idx_credits_balance ON credits(balance_usd) WHERE balance_usd > 0;
