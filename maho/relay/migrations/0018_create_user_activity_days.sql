-- Migration: 0018_create_user_activity_days.sql
--
-- Server-derived funnel/retention signal built from activity the relay already
-- sees for authenticated accounts. Records the UTC DAY only (unix_ts / 86400) —
-- no install_id, IP, UA, URL, device name, or sub-day time. This is service
-- operations data, not client telemetry: nothing new is sent from the browser
-- (see the zero-telemetry invariant). Pruned to 90 days by the scheduled job.

CREATE TABLE user_activity_days (
    user_id INTEGER NOT NULL,
    day     INTEGER NOT NULL,
    PRIMARY KEY (user_id, day)
);

CREATE INDEX idx_activity_day ON user_activity_days (day);
