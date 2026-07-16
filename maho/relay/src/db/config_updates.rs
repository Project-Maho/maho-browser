use serde::{Deserialize, Serialize};
use worker::*;

use crate::errors::Result;

use super::js;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ConfigUpdate {
    pub version: i32,
    pub payload: String,
    pub signature: String,
    pub rollout_threshold: i32,
    pub channel: String,
}

pub async fn find_latest_config_update(
    db: &D1Database,
    channel: &str,
    bucket: i32,
) -> Result<Option<ConfigUpdate>> {
    let stmt = db.prepare(
        "SELECT version, payload, signature, rollout_threshold, channel \
         FROM config_updates \
         WHERE channel = ?1 AND ?2 < rollout_threshold \
         ORDER BY version DESC \
         LIMIT 1",
    );
    let stmt = stmt.bind(&[js(&channel)?, js(&bucket)?])?;
    let rows = stmt.all().await?;
    let mut results: Vec<ConfigUpdate> = rows.results()?;
    Ok(results.pop())
}

pub async fn insert_config_update(db: &D1Database, update: &ConfigUpdate) -> Result<()> {
    let stmt = db.prepare(
        "INSERT OR REPLACE INTO config_updates \
         (version, payload, signature, rollout_threshold, channel) \
         VALUES (?1, ?2, ?3, ?4, ?5)",
    );
    let stmt = stmt.bind(&[
        js(&update.version)?,
        js(&update.payload)?,
        js(&update.signature)?,
        js(&update.rollout_threshold)?,
        js(&update.channel)?,
    ])?;
    stmt.run().await?;
    Ok(())
}

#[cfg(test)]
fn select_latest_config_update<'a>(
    candidates: &'a [ConfigUpdate],
    channel: &str,
    bucket: i32,
) -> Option<&'a ConfigUpdate> {
    candidates
        .iter()
        .filter(|config| config.channel == channel && bucket < config.rollout_threshold)
        .max_by_key(|config| config.version)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn update(version: i32, rollout_threshold: i32, channel: &str) -> ConfigUpdate {
        ConfigUpdate {
            version,
            payload: format!("{{\"version\":{version}}}"),
            signature: format!("signature-{version}"),
            rollout_threshold,
            channel: channel.to_string(),
        }
    }

    #[test]
    fn select_latest_config_update_returns_full_rollout_when_bucket_skips_partial() {
        let candidates = vec![
            update(1, 100, "stable"),
            update(2, 10, "stable"),
            update(99, 100, "beta"),
        ];

        let selected = super::select_latest_config_update(&candidates, "stable", 50);

        assert_eq!(selected.map(|config| config.version), Some(1));
    }

    #[test]
    fn select_latest_config_update_returns_highest_eligible_version_when_bucket_is_low() {
        let candidates = vec![
            update(1, 100, "stable"),
            update(2, 10, "stable"),
            update(99, 100, "beta"),
        ];

        let selected = super::select_latest_config_update(&candidates, "stable", 5);

        assert_eq!(selected.map(|config| config.version), Some(2));
    }

    #[test]
    fn select_latest_config_update_returns_none_when_bucket_reaches_max_threshold() {
        let candidates = vec![update(1, 100, "stable"), update(2, 10, "stable")];

        let selected = super::select_latest_config_update(&candidates, "stable", 100);

        assert!(selected.is_none());
    }

    #[test]
    fn select_latest_config_update_filters_by_channel_before_version_ordering() {
        let candidates = vec![update(1, 100, "stable"), update(99, 100, "beta")];

        let selected = super::select_latest_config_update(&candidates, "stable", 5);

        assert_eq!(selected.map(|config| config.version), Some(1));
    }

    #[test]
    fn select_latest_config_update_fails_closed_for_sentinel_bucket() {
        let candidates = vec![
            update(1, 100, "stable"), // threshold is 100, but sentinel 100 is not < 100
            update(2, 50, "stable"),
        ];

        let selected = super::select_latest_config_update(&candidates, "stable", 100);
        assert!(selected.is_none());
    }
}
