use serde::{Deserialize, Serialize};
use worker::*;

use crate::errors::Result;

use super::js;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct ReleaseUpdate {
    pub platform: String,
    #[serde(default = "default_channel")]
    pub channel: String,
    pub version: String,
    #[serde(default)]
    pub version_code: Option<i64>,
    #[serde(default)]
    pub rollout_threshold: Option<i32>,
    #[serde(default)]
    pub appcast_xml: Option<String>,
    #[serde(default)]
    pub win_envelope: Option<String>,
    #[serde(default)]
    pub notes_url: Option<String>,
    #[serde(default)]
    pub pub_date: Option<String>,
    #[serde(default)]
    pub created_at: Option<i64>,
}

fn default_channel() -> String {
    "stable".to_string()
}

pub async fn find_latest_macos_release(
    db: &D1Database,
    channel: &str,
    bucket: i32,
) -> Result<Option<ReleaseUpdate>> {
    let stmt = db.prepare(
        "SELECT platform, channel, version, version_code, rollout_threshold, \
         appcast_xml, win_envelope, notes_url, pub_date, created_at \
         FROM release_updates \
         WHERE platform = 'macos' AND channel = ?1 AND ?2 < rollout_threshold \
         ORDER BY version_code DESC \
         LIMIT 1",
    );
    let stmt = stmt.bind(&[js(&channel)?, js(&bucket)?])?;
    let rows = stmt.all().await?;
    let mut results: Vec<ReleaseUpdate> = rows.results()?;
    Ok(results.pop())
}

pub async fn find_latest_windows_release(
    db: &D1Database,
    channel: &str,
) -> Result<Option<ReleaseUpdate>> {
    let stmt = db.prepare(
        "SELECT platform, channel, version, version_code, rollout_threshold, \
         appcast_xml, win_envelope, notes_url, pub_date, created_at \
         FROM release_updates \
         WHERE platform = 'windows' AND channel = ?1 \
         ORDER BY version_code DESC \
         LIMIT 1",
    );
    let stmt = stmt.bind(&[js(&channel)?])?;
    let rows = stmt.all().await?;
    let mut results: Vec<ReleaseUpdate> = rows.results()?;
    Ok(results.pop())
}

pub async fn upsert_release_update(db: &D1Database, update: &ReleaseUpdate) -> Result<()> {
    let now = chrono::Utc::now().timestamp();
    let stmt = db.prepare(
        "INSERT INTO release_updates ( \
             platform, channel, version, version_code, rollout_threshold, \
             appcast_xml, win_envelope, notes_url, pub_date, created_at \
         ) VALUES (?1, ?2, ?3, ?4, COALESCE(?5, 100), ?6, ?7, ?8, ?9, COALESCE(?10, ?11)) \
         ON CONFLICT(platform, channel, version) DO UPDATE SET \
             version_code = COALESCE(excluded.version_code, release_updates.version_code), \
             rollout_threshold = COALESCE(?5, release_updates.rollout_threshold), \
             appcast_xml = COALESCE(excluded.appcast_xml, release_updates.appcast_xml), \
             win_envelope = COALESCE(excluded.win_envelope, release_updates.win_envelope), \
             notes_url = COALESCE(excluded.notes_url, release_updates.notes_url), \
             pub_date = COALESCE(excluded.pub_date, release_updates.pub_date), \
             created_at = COALESCE(release_updates.created_at, excluded.created_at)",
    );
    let stmt = stmt.bind(&[
        js(&update.platform)?,
        js(&update.channel)?,
        js(&update.version)?,
        js(&update.version_code)?,
        js(&update.rollout_threshold)?,
        js(&update.appcast_xml)?,
        js(&update.win_envelope)?,
        js(&update.notes_url)?,
        js(&update.pub_date)?,
        js(&update.created_at)?,
        js(&now)?,
    ])?;
    stmt.run().await?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use rusqlite::{params, Connection, OptionalExtension};

    fn setup_test_db() -> Connection {
        let db = Connection::open_in_memory().unwrap();
        db.execute_batch(include_str!(
            "../../migrations/0024_create_release_updates.sql"
        ))
        .unwrap();
        db
    }

    fn sqlite_upsert_release_update(db: &Connection, update: &ReleaseUpdate) {
        let now = chrono::Utc::now().timestamp();
        db.execute(
            "INSERT INTO release_updates (
                 platform, channel, version, version_code, rollout_threshold,
                 appcast_xml, win_envelope, notes_url, pub_date, created_at
             ) VALUES (?1, ?2, ?3, ?4, COALESCE(?5, 100), ?6, ?7, ?8, ?9, COALESCE(?10, ?11))
             ON CONFLICT(platform, channel, version) DO UPDATE SET
                 version_code = COALESCE(excluded.version_code, release_updates.version_code),
                 rollout_threshold = COALESCE(?5, release_updates.rollout_threshold),
                 appcast_xml = COALESCE(excluded.appcast_xml, release_updates.appcast_xml),
                 win_envelope = COALESCE(excluded.win_envelope, release_updates.win_envelope),
                 notes_url = COALESCE(excluded.notes_url, release_updates.notes_url),
                 pub_date = COALESCE(excluded.pub_date, release_updates.pub_date),
                 created_at = COALESCE(release_updates.created_at, excluded.created_at)",
            params![
                update.platform,
                update.channel,
                update.version,
                update.version_code,
                update.rollout_threshold,
                update.appcast_xml,
                update.win_envelope,
                update.notes_url,
                update.pub_date,
                update.created_at,
                now,
            ],
        )
        .unwrap();
    }

    fn sqlite_find_latest_macos(
        db: &Connection,
        channel: &str,
        bucket: i32,
    ) -> Option<ReleaseUpdate> {
        db.query_row(
            "SELECT platform, channel, version, version_code, rollout_threshold, \
             appcast_xml, win_envelope, notes_url, pub_date, created_at \
             FROM release_updates \
             WHERE platform = 'macos' AND channel = ?1 AND ?2 < rollout_threshold \
             ORDER BY version_code DESC \
             LIMIT 1",
            params![channel, bucket],
            |row| {
                Ok(ReleaseUpdate {
                    platform: row.get(0)?,
                    channel: row.get(1)?,
                    version: row.get(2)?,
                    version_code: row.get(3)?,
                    rollout_threshold: row.get(4)?,
                    appcast_xml: row.get(5)?,
                    win_envelope: row.get(6)?,
                    notes_url: row.get(7)?,
                    pub_date: row.get(8)?,
                    created_at: row.get(9)?,
                })
            },
        )
        .optional()
        .unwrap()
    }

    fn sqlite_find_latest_windows(db: &Connection, channel: &str) -> Option<ReleaseUpdate> {
        db.query_row(
            "SELECT platform, channel, version, version_code, rollout_threshold, \
             appcast_xml, win_envelope, notes_url, pub_date, created_at \
             FROM release_updates \
             WHERE platform = 'windows' AND channel = ?1 \
             ORDER BY version_code DESC \
             LIMIT 1",
            params![channel],
            |row| {
                Ok(ReleaseUpdate {
                    platform: row.get(0)?,
                    channel: row.get(1)?,
                    version: row.get(2)?,
                    version_code: row.get(3)?,
                    rollout_threshold: row.get(4)?,
                    appcast_xml: row.get(5)?,
                    win_envelope: row.get(6)?,
                    notes_url: row.get(7)?,
                    pub_date: row.get(8)?,
                    created_at: row.get(9)?,
                })
            },
        )
        .optional()
        .unwrap()
    }

    #[test]
    fn test_platform_filter() {
        let db = setup_test_db();

        sqlite_upsert_release_update(
            &db,
            &ReleaseUpdate {
                platform: "macos".to_string(),
                channel: "stable".to_string(),
                version: "1.0.0".to_string(),
                version_code: Some(100),
                rollout_threshold: Some(100),
                appcast_xml: Some("<xml>mac-1.0.0</xml>".to_string()),
                win_envelope: None,
                notes_url: None,
                pub_date: None,
                created_at: Some(1000),
            },
        );

        sqlite_upsert_release_update(
            &db,
            &ReleaseUpdate {
                platform: "windows".to_string(),
                channel: "stable".to_string(),
                version: "1.0.0".to_string(),
                version_code: Some(100),
                rollout_threshold: Some(100),
                appcast_xml: None,
                win_envelope: Some(r#"{"version":"1.0.0"}"#.to_string()),
                notes_url: None,
                pub_date: None,
                created_at: Some(1000),
            },
        );

        // macOS query gets macOS row, not Windows row
        let mac = sqlite_find_latest_macos(&db, "stable", 50).unwrap();
        assert_eq!(mac.platform, "macos");
        assert_eq!(mac.appcast_xml.as_deref(), Some("<xml>mac-1.0.0</xml>"));

        // Windows query gets Windows row, not macOS row
        let win = sqlite_find_latest_windows(&db, "stable").unwrap();
        assert_eq!(win.platform, "windows");
        assert_eq!(win.win_envelope.as_deref(), Some(r#"{"version":"1.0.0"}"#));

        // Different channel returns None
        assert!(sqlite_find_latest_macos(&db, "beta", 50).is_none());
        assert!(sqlite_find_latest_windows(&db, "beta").is_none());
    }

    #[test]
    fn test_mac_bucket_gating() {
        let db = setup_test_db();

        sqlite_upsert_release_update(
            &db,
            &ReleaseUpdate {
                platform: "macos".to_string(),
                channel: "stable".to_string(),
                version: "1.0.0".to_string(),
                version_code: Some(100),
                rollout_threshold: Some(100),
                appcast_xml: Some("<xml>1.0.0</xml>".to_string()),
                win_envelope: None,
                notes_url: None,
                pub_date: None,
                created_at: Some(1000),
            },
        );

        sqlite_upsert_release_update(
            &db,
            &ReleaseUpdate {
                platform: "macos".to_string(),
                channel: "stable".to_string(),
                version: "2.0.0".to_string(),
                version_code: Some(200),
                rollout_threshold: Some(20), // 20% canary rollout
                appcast_xml: Some("<xml>2.0.0</xml>".to_string()),
                win_envelope: None,
                notes_url: None,
                pub_date: None,
                created_at: Some(2000),
            },
        );

        // Bucket 10 (< 20) receives canary 2.0.0
        let res_low = sqlite_find_latest_macos(&db, "stable", 10).unwrap();
        assert_eq!(res_low.version, "2.0.0");

        // Bucket 20 (>= 20, but < 100) falls back to stable 1.0.0
        let res_mid = sqlite_find_latest_macos(&db, "stable", 20).unwrap();
        assert_eq!(res_mid.version, "1.0.0");

        // Bucket 100 (sentinel) is not < 100, so returns None
        let res_sentinel = sqlite_find_latest_macos(&db, "stable", 100);
        assert!(res_sentinel.is_none());
    }

    #[test]
    fn test_partial_upsert_preserves_sibling_and_null_columns() {
        let db = setup_test_db();

        // 1. Insert initial macos and windows rows for same version
        sqlite_upsert_release_update(
            &db,
            &ReleaseUpdate {
                platform: "macos".to_string(),
                channel: "stable".to_string(),
                version: "1.0.0".to_string(),
                version_code: Some(100),
                rollout_threshold: Some(50),
                appcast_xml: Some("<xml>mac-1.0.0</xml>".to_string()),
                win_envelope: None,
                notes_url: Some("https://notes/1.0.0".to_string()),
                pub_date: Some("2026-08-01".to_string()),
                created_at: Some(1000),
            },
        );

        sqlite_upsert_release_update(
            &db,
            &ReleaseUpdate {
                platform: "windows".to_string(),
                channel: "stable".to_string(),
                version: "1.0.0".to_string(),
                version_code: Some(100),
                rollout_threshold: Some(50),
                appcast_xml: None,
                win_envelope: Some(r#"{"version":"1.0.0"}"#.to_string()),
                notes_url: Some("https://notes/1.0.0".to_string()),
                pub_date: Some("2026-08-01".to_string()),
                created_at: Some(1000),
            },
        );

        // 2. Partial upsert for macOS: update rollout_threshold to 100, leaving appcast_xml/notes_url/pub_date None
        sqlite_upsert_release_update(
            &db,
            &ReleaseUpdate {
                platform: "macos".to_string(),
                channel: "stable".to_string(),
                version: "1.0.0".to_string(),
                version_code: None,
                rollout_threshold: Some(100),
                appcast_xml: None,
                win_envelope: None,
                notes_url: None,
                pub_date: None,
                created_at: None,
            },
        );

        // Verify macOS row preserved its existing non-null columns and original created_at
        let mac = sqlite_find_latest_macos(&db, "stable", 60).unwrap();
        assert_eq!(mac.rollout_threshold, Some(100));
        assert_eq!(mac.version_code, Some(100));
        assert_eq!(mac.appcast_xml.as_deref(), Some("<xml>mac-1.0.0</xml>"));
        assert_eq!(mac.notes_url.as_deref(), Some("https://notes/1.0.0"));
        assert_eq!(mac.pub_date.as_deref(), Some("2026-08-01"));
        assert_eq!(mac.created_at, Some(1000));

        // Verify Windows sibling row remains completely intact
        let win = sqlite_find_latest_windows(&db, "stable").unwrap();
        assert_eq!(win.platform, "windows");
        assert_eq!(win.version, "1.0.0");
        assert_eq!(win.win_envelope.as_deref(), Some(r#"{"version":"1.0.0"}"#));
        assert_eq!(win.rollout_threshold, Some(50));
    }
}
