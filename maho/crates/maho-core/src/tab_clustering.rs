use maho_types::tab::Tab;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

const DEFAULT_TIME_WINDOW_SECS: f64 = 1800.0;

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct TabClusterResult {
    pub domain: String,
    pub group_name: String,
    pub tab_ids: Vec<String>,
    pub first_active_at: String,
    pub last_active_at: String,
}

pub fn cluster_tabs(tabs: &[&Tab], threshold: f64) -> Vec<TabClusterResult> {
    let window_secs = if threshold.is_finite() && threshold > 0.0 {
        threshold as i64
    } else {
        DEFAULT_TIME_WINDOW_SECS as i64
    };

    let mut by_domain: HashMap<String, Vec<&Tab>> = HashMap::new();

    for tab in tabs {
        if let Some(domain) = etld_plus_one(&tab.url.0) {
            by_domain.entry(domain).or_default().push(tab);
        }
    }

    let mut results = Vec::new();

    for (domain, mut group_tabs) in by_domain {
        if group_tabs.len() < 2 {
            continue;
        }

        group_tabs.sort_by_key(|tab| parse_time(&tab.last_active_at.0));

        let mut current_ids: Vec<String> = Vec::new();
        let mut current_first_ts: i64 = 0;
        let mut prev_ts: i64 = 0;

        for tab in group_tabs {
            let ts = parse_time(&tab.last_active_at.0);
            if !current_ids.is_empty() && (ts - prev_ts).abs() > window_secs {
                if current_ids.len() >= 2 {
                    results.push(TabClusterResult {
                        domain: domain.clone(),
                        group_name: make_group_name(&domain),
                        tab_ids: current_ids.clone(),
                        first_active_at: format_time(current_first_ts),
                        last_active_at: format_time(prev_ts),
                    });
                }
                current_ids.clear();
                current_first_ts = ts;
            }
            if current_ids.is_empty() {
                current_first_ts = ts;
            }
            current_ids.push(tab.id.0.clone());
            prev_ts = ts;
        }

        if current_ids.len() >= 2 {
            results.push(TabClusterResult {
                domain: domain.clone(),
                group_name: make_group_name(&domain),
                tab_ids: current_ids,
                first_active_at: format_time(current_first_ts),
                last_active_at: format_time(prev_ts),
            });
        }
    }

    results
}

fn etld_plus_one(url_str: &str) -> Option<String> {
    let parsed = reqwest::Url::parse(url_str).ok()?;
    let host = parsed.host_str()?.to_lowercase();
    match psl::domain_str(&host) {
        Some(d) => Some(d.to_string()),
        None => Some(host),
    }
}

fn make_group_name(domain: &str) -> String {
    let root = domain.split('.').next().unwrap_or(domain);
    if root.is_empty() {
        return "General".to_string();
    }
    let mut chars = root.chars();
    match chars.next() {
        None => String::new(),
        Some(c) => c.to_uppercase().collect::<String>() + chars.as_str(),
    }
}

fn parse_time(time_str: &str) -> i64 {
    chrono::DateTime::parse_from_rfc3339(time_str)
        .map(|dt| dt.timestamp())
        .unwrap_or(0)
}

fn format_time(ts: i64) -> String {
    chrono::DateTime::from_timestamp(ts, 0)
        .map(|dt| dt.to_rfc3339())
        .unwrap_or_default()
}

#[cfg(test)]
mod tests {
    use super::*;
    use maho_types::common::{DateTime, ScrollPosition, Url};
    use maho_types::identifiers::{SpaceId, TabId};
    use maho_types::tab::{TabLifecycleState, TabRole};

    fn make_test_tab(id: &str, url: &str, time: &str) -> Tab {
        Tab {
            id: TabId::new(id),
            parent_id: None,
            space_id: SpaceId::new("space1"),
            url: Url(url.to_string()),
            title: "Test".to_string(),
            custom_title: None,
            custom_icon: None,
            favicon: None,
            state: TabLifecycleState::Active,
            role: TabRole::Normal,
            is_muted: false,
            zoom_level: 1.0,
            created_at: DateTime::from_iso(time),
            last_active_at: DateTime::from_iso(time),
            scroll_position: ScrollPosition::default(),
            pinned_url: None,
            window_id: None,
            is_private: false,
        }
    }

    #[test]
    fn test_cluster_tabs_by_domain_and_time_default_window() {
        let t1 = make_test_tab("1", "https://www.github.com/foo", "2026-07-09T10:00:00Z");
        let t2 = make_test_tab("2", "https://github.com/bar", "2026-07-09T10:20:00Z");
        let t3 = make_test_tab("3", "https://github.com/baz", "2026-07-09T11:30:00Z");
        let t4 = make_test_tab("4", "https://google.com", "2026-07-09T10:00:00Z");

        let tabs = vec![&t1, &t2, &t3, &t4];
        let clusters = cluster_tabs(&tabs, DEFAULT_TIME_WINDOW_SECS);

        assert_eq!(clusters.len(), 1, "expected exactly one github cluster");
        let c = &clusters[0];
        assert_eq!(c.domain, "github.com");
        assert_eq!(c.group_name, "Github");
        assert_eq!(c.tab_ids, vec!["1".to_string(), "2".to_string()]);
    }

    #[test]
    fn test_cluster_tabs_respects_threshold_parameter() {
        let t1 = make_test_tab("1", "https://a.example.com/1", "2026-07-09T10:00:00Z");
        let t2 = make_test_tab("2", "https://a.example.com/2", "2026-07-09T13:00:00Z");
        let tabs = vec![&t1, &t2];

        let strict = cluster_tabs(&tabs, 1800.0);
        assert!(
            strict.is_empty(),
            "3h gap must NOT cluster under 30-min window"
        );

        let relaxed = cluster_tabs(&tabs, 4.0 * 3600.0);
        assert_eq!(relaxed.len(), 1, "3h gap SHOULD cluster under 4h window");
    }

    #[test]
    fn test_cluster_tabs_uses_etld_plus_one() {
        let t1 = make_test_tab("1", "https://news.bbc.co.uk/x", "2026-07-09T10:00:00Z");
        let t2 = make_test_tab("2", "https://sport.bbc.co.uk/y", "2026-07-09T10:15:00Z");
        let tabs = vec![&t1, &t2];

        let clusters = cluster_tabs(&tabs, DEFAULT_TIME_WINDOW_SECS);
        assert_eq!(
            clusters.len(),
            1,
            "subdomains of bbc.co.uk must fold to eTLD+1"
        );
        assert_eq!(clusters[0].domain, "bbc.co.uk");
    }

    #[test]
    fn test_cluster_tabs_populates_timestamps() {
        let t1 = make_test_tab("1", "https://github.com/a", "2026-07-09T10:00:00Z");
        let t2 = make_test_tab("2", "https://github.com/b", "2026-07-09T10:20:00Z");
        let clusters = cluster_tabs(&[&t1, &t2], DEFAULT_TIME_WINDOW_SECS);

        assert_eq!(clusters.len(), 1);
        assert!(clusters[0]
            .first_active_at
            .starts_with("2026-07-09T10:00:00"));
        assert!(clusters[0]
            .last_active_at
            .starts_with("2026-07-09T10:20:00"));
    }
}
