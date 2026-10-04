use std::collections::HashMap;

use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};

use crate::embedding;
use crate::error::CoreError;
use maho_storage::sqlite::SqliteStorage;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BrowsingPattern {
    pub kind: PatternKind,
    pub description: String,
    pub confidence: f32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum PatternKind {
    FrequentSite {
        domain: String,
        visits_per_week: f32,
    },
    TimeOfDay {
        hour_range: (u8, u8),
        topic: String,
    },
    TopicCluster {
        topic: String,
        sample_urls: Vec<String>,
    },
}

pub struct PatternManager {
    pub last_run: Option<DateTime<Utc>>,
}

impl Default for PatternManager {
    fn default() -> Self {
        Self::new()
    }
}

impl PatternManager {
    pub fn new() -> Self {
        Self { last_run: None }
    }

    pub fn extract_patterns(
        &self,
        history: &[(String, String, Option<String>, String)],
        min_visits: u32,
    ) -> Vec<BrowsingPattern> {
        let mut patterns = Vec::new();
        patterns.extend(self.detect_frequent_sites(history, min_visits));
        patterns.extend(self.detect_time_of_day(history));
        patterns.extend(self.detect_topic_clusters(history));
        patterns
    }

    pub fn persist_patterns(
        &self,
        storage: &SqliteStorage,
        patterns: &[BrowsingPattern],
    ) -> Result<(), CoreError> {
        for pattern in patterns {
            let label = match &pattern.kind {
                PatternKind::FrequentSite { domain, .. } => {
                    format!("pattern:frequent:{domain}")
                }
                PatternKind::TimeOfDay { hour_range, .. } => {
                    format!("pattern:time:{}_{}", hour_range.0, hour_range.1)
                }
                PatternKind::TopicCluster { topic, .. } => {
                    format!("pattern:topic:{topic}")
                }
            };
            let token_count = (pattern.description.len() / 4) as i64;
            storage
                .upsert_memory_block(&label, &pattern.description, "warm", token_count)
                .map_err(|e| CoreError::Storage(e.to_string()))?;
        }
        Ok(())
    }

    pub fn build_pattern_context(&self, patterns: &[BrowsingPattern]) -> String {
        if patterns.is_empty() {
            return String::new();
        }
        let lines: Vec<&str> = patterns.iter().map(|p| p.description.as_str()).collect();
        lines.join("; ")
    }

    fn detect_frequent_sites(
        &self,
        history: &[(String, String, Option<String>, String)],
        min_visits: u32,
    ) -> Vec<BrowsingPattern> {
        let mut domain_counts: HashMap<String, u32> = HashMap::new();
        for (_id, url, _title, _visited_at) in history {
            if let Some(domain) = extract_domain(url) {
                *domain_counts.entry(domain).or_insert(0) += 1;
            }
        }
        domain_counts
            .into_iter()
            .filter(|(_, count)| *count >= min_visits)
            .map(|(domain, count)| {
                let visits_per_week = count as f32;
                let confidence = (visits_per_week / 20.0).min(1.0);
                BrowsingPattern {
                    kind: PatternKind::FrequentSite {
                        domain: domain.clone(),
                        visits_per_week,
                    },
                    description: format!(
                        "Frequently visits {domain} ({count} times/week)"
                    ),
                    confidence,
                }
            })
            .collect()
    }

    fn detect_time_of_day(
        &self,
        history: &[(String, String, Option<String>, String)],
    ) -> Vec<BrowsingPattern> {
        let mut hour_counts: [u32; 24] = [0; 24];
        let mut hour_domains: HashMap<u8, HashMap<String, u32>> = HashMap::new();

        for (_id, url, _title, visited_at) in history {
            let Ok(dt) = visited_at.parse::<DateTime<Utc>>() else {
                continue;
            };
            let hour = dt.format("%H").to_string().parse::<u8>().unwrap_or(0);
            hour_counts[hour as usize] += 1;
            if let Some(domain) = extract_domain(url) {
                *hour_domains
                    .entry(hour)
                    .or_default()
                    .entry(domain)
                    .or_insert(0) += 1;
            }
        }

        let total: u32 = hour_counts.iter().sum();
        if total == 0 {
            return Vec::new();
        }

        let avg = total as f32 / 24.0;
        let threshold = avg * 1.5; // top 25%-ish activity

        let mut patterns = Vec::new();
        let mut hour = 0u8;
        while hour < 24 {
            if (hour_counts[hour as usize] as f32) < threshold {
                hour += 1;
                continue;
            }
            // find contiguous peak range
            let start = hour;
            while hour < 24 && (hour_counts[hour as usize] as f32) >= threshold {
                hour += 1;
            }
            let end = hour; // exclusive

            // find dominant domain in this range
            let mut merged: HashMap<String, u32> = HashMap::new();
            for h in start..end {
                if let Some(domains) = hour_domains.get(&h) {
                    for (d, c) in domains {
                        *merged.entry(d.clone()).or_insert(0) += c;
                    }
                }
            }
            let top_domain = merged
                .into_iter()
                .max_by_key(|(_, c)| *c)
                .map(|(d, _)| d)
                .unwrap_or_default();

            let range_visits: u32 =
                (start..end).map(|h| hour_counts[h as usize]).sum();
            let confidence = (range_visits as f32 / total as f32).min(1.0);

            patterns.push(BrowsingPattern {
                kind: PatternKind::TimeOfDay {
                    hour_range: (start, end),
                    topic: top_domain.clone(),
                },
                description: format!(
                    "Active {start}:00-{end}:00 UTC, often visiting {top_domain}"
                ),
                confidence,
            });
        }
        patterns
    }

    fn detect_topic_clusters(
        &self,
        history: &[(String, String, Option<String>, String)],
    ) -> Vec<BrowsingPattern> {
        let entries: Vec<(String, String)> = history
            .iter()
            .filter_map(|(_id, url, title, _visited_at)| {
                let t = title.as_deref().unwrap_or("");
                if t.is_empty() {
                    return None;
                }
                Some((t.to_string(), url.clone()))
            })
            .collect();

        if entries.len() < 3 {
            return Vec::new();
        }

        let texts: Vec<String> = entries.iter().map(|(t, _)| t.clone()).collect();
        let embeddings = match embedding::embed(&texts) {
            Ok(e) => e,
            Err(_) => return Vec::new(),
        };

        // greedy clustering
        let similarity_threshold = 0.7;
        let mut clusters: Vec<Vec<usize>> = Vec::new();
        for i in 0..entries.len() {
            let mut joined = false;
            for cluster in clusters.iter_mut() {
                let centroid_idx = cluster[0];
                let sim = cosine_similarity(&embeddings[i], &embeddings[centroid_idx]);
                if sim >= similarity_threshold {
                    cluster.push(i);
                    joined = true;
                    break;
                }
            }
            if !joined {
                clusters.push(vec![i]);
            }
        }

        clusters
            .into_iter()
            .filter(|c| c.len() >= 3)
            .map(|cluster| {
                let representative_title = entries[cluster[0]].0.clone();
                let sample_urls: Vec<String> = cluster
                    .iter()
                    .take(5)
                    .map(|&i| entries[i].1.clone())
                    .collect();
                let confidence = ((cluster.len() as f32) / 10.0).min(1.0);
                BrowsingPattern {
                    kind: PatternKind::TopicCluster {
                        topic: representative_title.clone(),
                        sample_urls: sample_urls.clone(),
                    },
                    description: format!(
                        "Topic cluster: {} ({} pages)",
                        representative_title,
                        cluster.len()
                    ),
                    confidence,
                }
            })
            .collect()
    }
}

fn extract_domain(url: &str) -> Option<String> {
    let without_scheme = url
        .strip_prefix("https://")
        .or_else(|| url.strip_prefix("http://"))
        .unwrap_or(url);
    let host = without_scheme.split('/').next()?;
    let host = host.split(':').next()?;
    if host.is_empty() {
        return None;
    }
    Some(host.to_string())
}

fn cosine_similarity(a: &[f32], b: &[f32]) -> f32 {
    let dot: f32 = a.iter().zip(b).map(|(x, y)| x * y).sum();
    let norm_a: f32 = a.iter().map(|x| x * x).sum::<f32>().sqrt();
    let norm_b: f32 = b.iter().map(|x| x * x).sum::<f32>().sqrt();
    if norm_a == 0.0 || norm_b == 0.0 {
        0.0
    } else {
        dot / (norm_a * norm_b)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_session(url: &str, title: &str, visited_at: &str) -> (String, String, Option<String>, String) {
        (
            uuid::Uuid::new_v4().to_string(),
            url.to_string(),
            Some(title.to_string()),
            visited_at.to_string(),
        )
    }

    #[test]
    fn test_extract_frequent_sites() {
        let pm = PatternManager::new();
        let mut history = Vec::new();
        for i in 0..10 {
            history.push(make_session(
                &format!("https://github.com/repo/{i}"),
                "GitHub",
                "2026-06-10T10:00:00Z",
            ));
        }
        // add some noise
        history.push(make_session("https://example.com", "Example", "2026-06-10T10:00:00Z"));

        let patterns = pm.extract_patterns(&history, 5);
        let freq: Vec<_> = patterns
            .iter()
            .filter(|p| matches!(&p.kind, PatternKind::FrequentSite { domain, .. } if domain == "github.com"))
            .collect();
        assert_eq!(freq.len(), 1);
        assert!(freq[0].confidence >= 0.5);
    }

    #[test]
    fn test_singleton_filtered() {
        let pm = PatternManager::new();
        let history = vec![
            make_session("https://rare-site.com/a", "Rare A", "2026-06-10T10:00:00Z"),
            make_session("https://rare-site.com/b", "Rare B", "2026-06-10T11:00:00Z"),
        ];
        let patterns = pm.detect_frequent_sites(&history, 5);
        assert!(
            patterns.is_empty(),
            "2 visits should NOT be a frequent pattern"
        );
    }

    #[test]
    fn test_extract_time_of_day() {
        let pm = PatternManager::new();
        let mut history = Vec::new();
        // cluster visits at 8am UTC
        for _ in 0..20 {
            history.push(make_session(
                "https://news.ycombinator.com",
                "Hacker News",
                "2026-06-10T08:30:00Z",
            ));
        }
        // spread noise across other hours
        for h in 0..24u8 {
            if h == 8 { continue; }
            history.push(make_session(
                "https://example.com",
                "Example",
                &format!("2026-06-10T{h:02}:00:00Z"),
            ));
        }

        let patterns = pm.detect_time_of_day(&history);
        let time_patterns: Vec<_> = patterns
            .iter()
            .filter(|p| matches!(&p.kind, PatternKind::TimeOfDay { hour_range, .. } if hour_range.0 <= 8 && hour_range.1 > 8))
            .collect();
        assert!(
            !time_patterns.is_empty(),
            "Should detect activity peak at hour 8"
        );
    }

    #[test]
    #[ignore] // requires embedding model at runtime
    fn test_extract_topic_cluster() {
        let pm = PatternManager::new();
        let history = vec![
            make_session("https://blog.rust-lang.org/2026", "Rust 2026 roadmap", "2026-06-10T10:00:00Z"),
            make_session("https://doc.rust-lang.org/book", "The Rust Programming Language", "2026-06-10T10:05:00Z"),
            make_session("https://crates.io/crates/tokio", "tokio - async runtime for Rust", "2026-06-10T10:10:00Z"),
            make_session("https://rust-lang.org/learn", "Learn Rust", "2026-06-10T10:15:00Z"),
            make_session("https://docs.rs/serde", "serde documentation - Rust", "2026-06-10T10:20:00Z"),
        ];

        let patterns = pm.detect_topic_clusters(&history);
        let clusters: Vec<_> = patterns
            .iter()
            .filter(|p| matches!(&p.kind, PatternKind::TopicCluster { .. }))
            .collect();
        assert!(
            !clusters.is_empty(),
            "5 Rust pages should form at least 1 topic cluster"
        );
    }

    #[test]
    fn test_build_pattern_context() {
        let pm = PatternManager::new();
        let patterns = vec![
            BrowsingPattern {
                kind: PatternKind::FrequentSite { domain: "github.com".into(), visits_per_week: 15.0 },
                description: "Frequently visits github.com (15 times/week)".into(),
                confidence: 0.75,
            },
            BrowsingPattern {
                kind: PatternKind::TimeOfDay { hour_range: (8, 10), topic: "news.ycombinator.com".into() },
                description: "Active 8:00-10:00 UTC, often visiting news.ycombinator.com".into(),
                confidence: 0.5,
            },
            BrowsingPattern {
                kind: PatternKind::TopicCluster { topic: "Rust programming".into(), sample_urls: vec![] },
                description: "Topic cluster: Rust programming (5 pages)".into(),
                confidence: 0.5,
            },
        ];
        let ctx = pm.build_pattern_context(&patterns);
        assert!(!ctx.is_empty());
        assert!(ctx.len() < 500);
        assert!(ctx.contains("github.com"));
        assert!(ctx.contains("Rust programming"));
    }

    #[test]
    fn test_extract_domain() {
        assert_eq!(extract_domain("https://github.com/repo/x"), Some("github.com".into()));
        assert_eq!(extract_domain("http://localhost:8080/path"), Some("localhost".into()));
        assert_eq!(extract_domain("https://sub.domain.co.uk/p"), Some("sub.domain.co.uk".into()));
        assert_eq!(extract_domain(""), None);
    }

    #[test]
    fn test_persist_patterns() {
        let storage = SqliteStorage::open_in_memory().unwrap();
        let pm = PatternManager::new();
        let patterns = vec![
            BrowsingPattern {
                kind: PatternKind::FrequentSite { domain: "github.com".into(), visits_per_week: 10.0 },
                description: "Frequently visits github.com (10 times/week)".into(),
                confidence: 0.5,
            },
        ];
        pm.persist_patterns(&storage, &patterns).unwrap();
        let block = storage.get_memory_block("pattern:frequent:github.com").unwrap();
        assert_eq!(block, Some("Frequently visits github.com (10 times/week)".into()));
    }
}
