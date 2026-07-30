-- Migration: 0020_create_order_refunds.sql
--
-- Tracks cumulative refunded cents per order so order_refunded webhooks deduct
-- only the incremental refund (LemonSqueezy sends the cumulative refunded_amount
-- on each event). Previously the full order total was deducted on every
-- order_refunded, over-charging on partial and repeated refunds.

CREATE TABLE order_refunds (
    order_id       TEXT NOT NULL PRIMARY KEY,
    refunded_cents INTEGER NOT NULL,
    updated_at     INTEGER NOT NULL
);
