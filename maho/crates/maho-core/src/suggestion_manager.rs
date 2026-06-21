use std::cmp::Ordering;
use std::collections::HashMap;

use chrono::{DateTime, Timelike, Utc};
use serde::{Deserialize, Serialize};

use crate::pattern_manager::{BrowsingPattern, PatternKind};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Suggestion {
    pub id: String,
    pub title: String,
    pub description: Option<String>,
    pub action: SuggestionAction,
    pub confidence: f32,
    pub kind: SuggestionKind,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum SuggestionAction {
    OpenUrl(String),
    OpenChat { initial_prompt: String },
    SearchHistory(String),
    Dismiss,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "snake_case")]
pub enum SuggestionKind {
    NewTab,
    Idle,
    PatternMatch,
    TimeOfDay,
}

#[derive(Debug, Clone, PartialEq)]
pub enum Trigger {
    NewTab,
    Idle { seconds: u64 },
    PageLoaded { url: String },
}

pub struct SuggestionManager {
    recent_dismissals: HashMap<String, DateTime<Utc>>,
    dedup_window_minutes: i64,
}

impl Default for SuggestionManager {
    fn default() -> Self {
        Self::new()
    }
}

impl SuggestionManager {
    pub fn new() -> Self {
        Self {
            recent_dismissals: HashMap::new(),
            dedup_window_minutes: 60,
        }
    }

    pub fn suggest(&self, trigger: &Trigger, patterns: &[BrowsingPattern]) -> Vec<Suggestion> {
        let mut suggestions = Vec::new();

        match trigger {
            Trigger::NewTab => {
                for p in patterns {
                    if let PatternKind::TimeOfDay {
                        hour_range,
                        ref topic,
                    } = p.kind
                    {
                        let now_hour = Utc::now().hour() as u8;
                        if now_hour >= hour_range.0 && now_hour <= hour_range.1 {
                            suggestions.push(Suggestion {
                                id: uuid::Uuid::new_v4().to_string(),
                                title: format!("Resume your {} routine?", topic),
                                description: Some(p.description.clone()),
                                action: SuggestionAction::SearchHistory(topic.clone()),
                                confidence: p.confidence,
                                kind: SuggestionKind::TimeOfDay,
                            });
                        }
                    }
                    if let PatternKind::FrequentSite { ref domain, .. } = p.kind {
                        suggestions.push(Suggestion {
                            id: uuid::Uuid::new_v4().to_string(),
                            title: format!("Open {}?", domain),
                            description: None,
                            action: SuggestionAction::OpenUrl(format!("https://{}", domain)),
                            confidence: p.confidence,
                            kind: SuggestionKind::PatternMatch,
                        });
                    }
                }
            }
            Trigger::Idle { seconds } if *seconds > 60 => {
                suggestions.push(Suggestion {
                    id: uuid::Uuid::new_v4().to_string(),
                    title: "Want to summarize this page?".to_string(),
                    description: None,
                    action: SuggestionAction::OpenChat {
                        initial_prompt: "Summarize this page".to_string(),
                    },
                    confidence: 0.7,
                    kind: SuggestionKind::Idle,
                });
            }
            Trigger::PageLoaded { ref url } => {
                for p in patterns {
                    if let PatternKind::TopicCluster {
                        ref topic,
                        ref sample_urls,
                    } = p.kind
                    {
                        if sample_urls.iter().any(|u| u != url) {
                            suggestions.push(Suggestion {
                                id: uuid::Uuid::new_v4().to_string(),
                                title: format!("Explore more about {}?", topic),
                                description: None,
                                action: SuggestionAction::SearchHistory(topic.clone()),
                                confidence: p.confidence,
                                kind: SuggestionKind::PatternMatch,
                            });
                            break;
                        }
                    }
                }
            }
            _ => {}
        }

        // Rank by confidence DESC
        suggestions.sort_by(|a, b| {
            b.confidence
                .partial_cmp(&a.confidence)
                .unwrap_or(Ordering::Equal)
        });

        // Deduplicate by title
        suggestions.dedup_by(|a, b| a.title == b.title);

        // Filter recently dismissed
        let cutoff = Utc::now() - chrono::Duration::minutes(self.dedup_window_minutes);
        suggestions.retain(|s| {
            self.recent_dismissals
                .get(&s.title)
                .map(|t| *t < cutoff)
                .unwrap_or(true)
        });

        // Cap at 5
        suggestions.truncate(5);
        suggestions
    }

    pub fn record_dismissal(&mut self, suggestion_title: &str) {
        self.recent_dismissals
            .insert(suggestion_title.to_string(), Utc::now());
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::pattern_manager::{BrowsingPattern, PatternKind};

    fn time_pattern(hour_start: u8, hour_end: u8, topic: &str, confidence: f32) -> BrowsingPattern {
        BrowsingPattern {
            kind: PatternKind::TimeOfDay {
                hour_range: (hour_start, hour_end),
                topic: topic.to_string(),
            },
            description: format!("Active {}:00-{}:00, visiting {}", hour_start, hour_end, topic),
            confidence,
        }
    }

    fn freq_pattern(domain: &str, confidence: f32) -> BrowsingPattern {
        BrowsingPattern {
            kind: PatternKind::FrequentSite {
                domain: domain.to_string(),
                visits_per_week: 10.0,
            },
            description: format!("Frequently visits {}", domain),
            confidence,
        }
    }

    fn topic_pattern(topic: &str, urls: Vec<&str>, confidence: f32) -> BrowsingPattern {
        BrowsingPattern {
            kind: PatternKind::TopicCluster {
                topic: topic.to_string(),
                sample_urls: urls.into_iter().map(String::from).collect(),
            },
            description: format!("Topic cluster: {}", topic),
            confidence,
        }
    }

    #[test]
    fn test_suggest_new_tab_with_time_pattern() {
        let sm = SuggestionManager::new();
        // Use hour range 0..23 to always match regardless of test execution time
        let patterns = vec![time_pattern(0, 23, "news", 0.8)];
        let results = sm.suggest(&Trigger::NewTab, &patterns);
        assert!(
            results.iter().any(|s| s.title.contains("news") && s.kind == SuggestionKind::TimeOfDay),
            "Should return a time-of-day suggestion for news"
        );
    }

    #[test]
    fn test_suggest_idle_summarize() {
        let sm = SuggestionManager::new();
        let results = sm.suggest(&Trigger::Idle { seconds: 120 }, &[]);
        assert_eq!(results.len(), 1);
        assert_eq!(results[0].title, "Want to summarize this page?");
        assert_eq!(results[0].kind, SuggestionKind::Idle);
    }

    #[test]
    fn test_suggest_idle_short_no_suggestion() {
        let sm = SuggestionManager::new();
        let results = sm.suggest(&Trigger::Idle { seconds: 30 }, &[]);
        assert!(results.is_empty(), "Idle < 60s should not trigger suggestion");
    }

    #[test]
    fn test_suggest_dedup() {
        let mut sm = SuggestionManager::new();
        let patterns = vec![freq_pattern("github.com", 0.9)];

        // First call should return suggestion
        let results = sm.suggest(&Trigger::NewTab, &patterns);
        assert!(!results.is_empty());

        // Dismiss it
        sm.record_dismissal("Open github.com?");

        // Second call should filter it out
        let results = sm.suggest(&Trigger::NewTab, &patterns);
        assert!(
            !results.iter().any(|s| s.title == "Open github.com?"),
            "Dismissed suggestion should not reappear within dedup window"
        );
    }

    #[test]
    fn test_suggest_ranked_by_confidence() {
        let sm = SuggestionManager::new();
        let patterns = vec![
            freq_pattern("low.com", 0.3),
            freq_pattern("high.com", 0.9),
            freq_pattern("mid.com", 0.6),
        ];
        let results = sm.suggest(&Trigger::NewTab, &patterns);
        assert!(results.len() >= 3);
        assert!(
            results[0].confidence >= results[1].confidence
                && results[1].confidence >= results[2].confidence,
            "Suggestions must be sorted by confidence DESC"
        );
    }

    #[test]
    fn test_suggest_truncated_to_5() {
        let sm = SuggestionManager::new();
        let patterns: Vec<BrowsingPattern> = (0..10)
            .map(|i| freq_pattern(&format!("site{}.com", i), 0.5 + (i as f32) * 0.01))
            .collect();
        let results = sm.suggest(&Trigger::NewTab, &patterns);
        assert!(
            results.len() <= 5,
            "Should truncate to max 5 suggestions, got {}",
            results.len()
        );
    }

    #[test]
    fn test_suggest_page_loaded_topic_cluster() {
        let sm = SuggestionManager::new();
        let patterns = vec![topic_pattern(
            "Rust programming",
            vec!["https://rust-lang.org", "https://crates.io"],
            0.8,
        )];
        let results = sm.suggest(
            &Trigger::PageLoaded {
                url: "https://rust-lang.org".to_string(),
            },
            &patterns,
        );
        assert!(
            results
                .iter()
                .any(|s| s.title.contains("Rust programming")),
            "Should suggest exploring topic cluster"
        );
    }
}
