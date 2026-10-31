//! Hybrid lexical (FTS5) + dense (HNSW) retrieval pipeline with Reciprocal Rank Fusion (RRF).

use serde::{Deserialize, Serialize};
use std::collections::HashMap;

/// Structured retrieval result combining lexical and dense rank evidence.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct HybridMemoryResult {
    pub id: String,
    pub fact: String,
    pub score: f64,
    pub lexical_rank: Option<usize>,
    pub dense_rank: Option<usize>,
    pub importance: f32,
    pub confidence: f32,
    pub source: String,
}

pub struct HybridMemoryRetriever {
    /// RRF smoothing constant (standard is 60.0)
    pub rrf_k: f64,
    /// Default character budget for context assembly
    pub default_char_budget: usize,
}

impl Default for HybridMemoryRetriever {
    fn default() -> Self {
        Self {
            rrf_k: 60.0,
            default_char_budget: 12000,
        }
    }
}

impl HybridMemoryRetriever {
    pub fn new(rrf_k: f64) -> Self {
        Self {
            rrf_k,
            default_char_budget: 12000,
        }
    }

    /// Fuse lexical FTS results and dense HNSW candidates using Reciprocal Rank Fusion (RRF).
    /// If dense candidates are absent/empty, gracefully falls back to ranked lexical FTS candidates.
    pub fn fuse(
        &self,
        lexical_candidates: &[(String, String, f64)], // (id, fact, fts_score)
        dense_candidates: &[(String, f32)],           // (id, distance)
        facts_metadata: &HashMap<String, (f32, f32, String, String)>, // id -> (importance, confidence, source, fact)
        top_k: usize,
        char_budget: Option<usize>,
    ) -> Vec<HybridMemoryResult> {
        if top_k == 0 {
            return Vec::new();
        }

        let mut lexical_ranks: HashMap<String, (usize, String)> = HashMap::new();
        for (rank, (id, fact, _)) in lexical_candidates.iter().enumerate() {
            lexical_ranks.insert(id.clone(), (rank + 1, fact.clone()));
        }

        let mut dense_ranks: HashMap<String, usize> = HashMap::new();
        for (rank, (id, _)) in dense_candidates.iter().enumerate() {
            dense_ranks.insert(id.clone(), rank + 1);
        }

        // Collect all distinct IDs
        let mut all_ids: Vec<String> = lexical_ranks
            .keys()
            .chain(dense_ranks.keys())
            .cloned()
            .collect();
        all_ids.sort();
        all_ids.dedup();

        let mut results = Vec::new();
        for id in all_ids {
            let (importance, confidence, source, fact_text) =
                facts_metadata.get(&id).cloned().unwrap_or_else(|| {
                    let text = lexical_ranks
                        .get(&id)
                        .map(|(_, text)| text.clone())
                        .unwrap_or_else(|| id.clone());
                    (0.5, 1.0, "fts".to_string(), text)
                });

            let lex_rank = lexical_ranks.get(&id).map(|(r, _)| *r);
            let dense_rank = dense_ranks.get(&id).copied();

            let lex_score = lex_rank
                .map(|r| 1.0 / (self.rrf_k + r as f64))
                .unwrap_or(0.0);
            let dense_score = dense_rank
                .map(|r| 1.0 / (self.rrf_k + r as f64))
                .unwrap_or(0.0);

            let rrf = lex_score + dense_score;

            let importance_factor = (0.5 + 0.5 * importance as f64).clamp(0.1, 1.5);
            let confidence_factor = (0.5 + 0.5 * confidence as f64).clamp(0.1, 1.5);

            let final_score = rrf * importance_factor * confidence_factor;

            results.push(HybridMemoryResult {
                id,
                fact: fact_text,
                score: final_score,
                lexical_rank: lex_rank,
                dense_rank,
                importance,
                confidence,
                source,
            });
        }

        // Sort descending by final score
        results.sort_unstable_by(|a, b| {
            b.score
                .partial_cmp(&a.score)
                .unwrap_or(std::cmp::Ordering::Equal)
        });

        let budget = char_budget.unwrap_or(self.default_char_budget);
        let mut budgeted = Vec::new();
        let mut used_chars = 0;

        for item in results.into_iter().take(top_k) {
            let next = used_chars + item.fact.len() + 4;
            if next > budget && !budgeted.is_empty() {
                break;
            }
            used_chars = next;
            budgeted.push(item);
        }

        budgeted
    }

    /// Format retrieved memory results into a prompt section.
    pub fn format_prompt_block(&self, results: &[HybridMemoryResult]) -> String {
        if results.is_empty() {
            return String::new();
        }
        let mut out = String::from("\n\nRetrieved Memories:\n");
        for item in results {
            out.push_str(&format!("- {}\n", item.fact));
        }
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rrf_fusion_favors_items_present_in_both_rankings() {
        let retriever = HybridMemoryRetriever::default();
        let lexical = vec![
            ("f1".to_string(), "fact 1".to_string(), 1.0),
            ("f2".to_string(), "fact 2".to_string(), 0.8),
        ];
        let dense = vec![("f2".to_string(), 0.1f32), ("f3".to_string(), 0.2f32)];

        let mut meta = HashMap::new();
        meta.insert(
            "f1".to_string(),
            (0.5, 1.0, "user".to_string(), "fact 1".to_string()),
        );
        meta.insert(
            "f2".to_string(),
            (0.5, 1.0, "user".to_string(), "fact 2".to_string()),
        );
        meta.insert(
            "f3".to_string(),
            (0.5, 1.0, "user".to_string(), "fact 3".to_string()),
        );

        let results = retriever.fuse(&lexical, &dense, &meta, 10, None);
        assert!(!results.is_empty());
        // f2 is ranked in both, so its RRF score is higher
        assert_eq!(results[0].id, "f2");
    }

    #[test]
    fn rrf_fusion_falls_back_to_lexical_when_dense_is_empty() {
        let retriever = HybridMemoryRetriever::default();
        let lexical = vec![
            ("f1".to_string(), "Rust borrow checker".to_string(), 1.5),
            ("f2".to_string(), "Rust lifetime semantics".to_string(), 1.2),
        ];
        let dense = vec![];
        let meta = HashMap::new();

        let results = retriever.fuse(&lexical, &dense, &meta, 5, None);
        assert_eq!(results.len(), 2);
        assert_eq!(results[0].id, "f1");
        assert_eq!(results[1].id, "f2");
        assert!(results[0].score > results[1].score);
    }
}
