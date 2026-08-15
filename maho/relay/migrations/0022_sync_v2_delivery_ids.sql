-- Migration: 0022_sync_v2_delivery_ids.sql
--
-- Protocol V2 transport idempotency. Every V2 envelope carries a client
-- generated delivery_id; a retry after a lost ACK must return the sequence the
-- relay already assigned instead of appending a second copy of the same
-- encrypted payload. Nullable for backward compatibility: the existing V1 log
-- rows have no delivery id and must stay readable and replayable.
--
-- The unique index is room-scoped and partial, so two different rooms may reuse
-- the same delivery id (they are independent logs) and legacy NULL rows never
-- collide with each other.

ALTER TABLE messages ADD COLUMN delivery_id TEXT;

CREATE UNIQUE INDEX idx_messages_room_delivery
    ON messages (room_id, delivery_id)
    WHERE delivery_id IS NOT NULL;
