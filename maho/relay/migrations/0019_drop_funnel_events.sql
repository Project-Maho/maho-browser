-- Migration: 0019_drop_funnel_events.sql
--
-- funnel_events (retracted 0013) was never part of a committed release, but
-- this DROP IF EXISTS is an idempotent safety net for any environment where a
-- pre-release 0013 was applied. The funnel is now derived server-side from
-- user_activity_days (0018); no client-generated install_id exists.

DROP TABLE IF EXISTS funnel_events;
