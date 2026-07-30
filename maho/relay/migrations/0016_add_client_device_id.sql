-- Migration: 0016_add_client_device_id.sql
--
-- A client-generated stable device identifier so re-login on the same device
-- reuses its row (and its sync room_id) instead of minting a new one every
-- time. Nullable for backward compatibility with clients that do not send it.
-- Partial unique index: one device per (user, client_device_id) when present;
-- legacy NULL rows are exempt so they never collide.

ALTER TABLE devices ADD COLUMN client_device_id TEXT;

CREATE UNIQUE INDEX idx_devices_user_client
    ON devices (user_id, client_device_id)
    WHERE client_device_id IS NOT NULL;
