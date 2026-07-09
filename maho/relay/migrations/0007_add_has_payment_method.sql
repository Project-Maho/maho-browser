-- Migration: 0007_add_has_payment_method.sql

ALTER TABLE users ADD COLUMN has_payment_method BOOLEAN NOT NULL DEFAULT 0;
ALTER TABLE users ADD COLUMN lemonsqueezy_customer_id TEXT;
