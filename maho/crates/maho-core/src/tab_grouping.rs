//! Smart tab grouping via semantic clustering.
//!
//! Uses BGE embeddings to cluster similar tabs by title+URL content.
//! Greedy threshold-based clustering — suitable for <100 tabs.

use maho_types::identifiers::TabId;

/// Lightweight tab info for clustering (avoids coupling to full Tab struct).
pub struct TabInfo {
    pub id: TabId,
    pub title: String,
    pub url: String,
}

/// A cluster of semantically related tabs.
#[derive(serde::Serialize)]
pub struct TabCluster {
    /// Auto-generated group name (title of the most representative tab).
    pub group_name: String,
    /// IDs of tabs belonging to this cluster.
    pub tab_ids: Vec<TabId>,
}

/// Cluster tabs by semantic similarity of their title+URL text.
///
/// Uses greedy first-fit: each tab joins the first cluster whose centroid
/// (first member) has cosine similarity >= `similarity_threshold`.
/// Singleton clusters (only 1 tab) are filtered out.
///
/// Recommended threshold: 0.75 for BGE-small embeddings.
pub fn cluster_tabs(tabs: &[TabInfo], similarity_threshold: f32) -> Vec<TabCluster> {
    if tabs.len() < 2 {
        return Vec::new();
    }

    let texts: Vec<String> = tabs
        .iter()
        .map(|t| format!("{} {}", t.title, t.url))
        .collect();

    let embeddings = match crate::embedding::embed(&texts) {
        Ok(e) => e,
        Err(_) => return Vec::new(),
    };

    // Greedy clustering: assign each tab to the first matching cluster
    let mut clusters: Vec<Vec<usize>> = Vec::new();
    for i in 0..tabs.len() {
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

    // Build TabCluster results, filtering out singletons
    clusters
        .into_iter()
        .filter(|c| c.len() >= 2)
        .map(|cluster| {
            let group_name = tabs[cluster[0]].title.clone();
            let tab_ids = cluster.into_iter().map(|i| tabs[i].id.clone()).collect();
            TabCluster { group_name, tab_ids }
        })
        .collect()
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

    #[test]
    fn test_cosine_similarity_identical() {
        let v = vec![1.0, 0.0, 0.0];
        assert!((cosine_similarity(&v, &v) - 1.0).abs() < 1e-6);
    }

    #[test]
    fn test_cosine_similarity_orthogonal() {
        let a = vec![1.0, 0.0, 0.0];
        let b = vec![0.0, 1.0, 0.0];
        assert!(cosine_similarity(&a, &b).abs() < 1e-6);
    }

    #[test]
    fn test_cosine_similarity_zero_vector() {
        let a = vec![1.0, 2.0, 3.0];
        let b = vec![0.0, 0.0, 0.0];
        assert_eq!(cosine_similarity(&a, &b), 0.0);
    }

    #[test]
    fn test_cluster_empty_input() {
        let tabs: Vec<TabInfo> = Vec::new();
        let clusters = cluster_tabs(&tabs, 0.75);
        assert!(clusters.is_empty());
    }

    #[test]
    fn test_cluster_single_tab() {
        let tabs = vec![TabInfo {
            id: TabId::new("t1"),
            title: "Hello".to_string(),
            url: "https://example.com".to_string(),
        }];
        let clusters = cluster_tabs(&tabs, 0.75);
        assert!(clusters.is_empty());
    }

    #[test]
    fn test_cluster_singleton_filtered() {
        // With a very high threshold, nothing should cluster together
        // (unless texts are nearly identical)
        let tabs = vec![
            TabInfo {
                id: TabId::new("t1"),
                title: "Quantum physics lecture notes".to_string(),
                url: "https://physics.edu/quantum".to_string(),
            },
            TabInfo {
                id: TabId::new("t2"),
                title: "Best chocolate cake recipe".to_string(),
                url: "https://cooking.com/cake".to_string(),
            },
            TabInfo {
                id: TabId::new("t3"),
                title: "Tokyo train schedule 2026".to_string(),
                url: "https://jreast.co.jp/schedule".to_string(),
            },
        ];
        // threshold=0.99 means nearly nothing clusters
        let clusters = cluster_tabs(&tabs, 0.99);
        // All should be singletons → filtered out
        assert!(clusters.is_empty());
    }

    /// Integration test: similar tabs should cluster when using real embeddings.
    /// This test requires the embedding model to be available at runtime.
    /// It is ignored by default for CI environments without the model.
    #[test]
    #[ignore]
    fn test_cluster_similar_tabs_grouped() {
        let tabs = vec![
            // Tech cluster
            TabInfo {
                id: TabId::new("t1"),
                title: "Introduction to Rust programming".to_string(),
                url: "https://rust-lang.org/learn".to_string(),
            },
            TabInfo {
                id: TabId::new("t2"),
                title: "Rust async programming guide".to_string(),
                url: "https://rust-lang.org/async".to_string(),
            },
            TabInfo {
                id: TabId::new("t3"),
                title: "Rust ownership and borrowing explained".to_string(),
                url: "https://doc.rust-lang.org/book/ch04".to_string(),
            },
            // Cooking cluster
            TabInfo {
                id: TabId::new("t4"),
                title: "Italian pasta recipes".to_string(),
                url: "https://cooking.com/italian-pasta".to_string(),
            },
            TabInfo {
                id: TabId::new("t5"),
                title: "Homemade pasta sauce recipe".to_string(),
                url: "https://cooking.com/pasta-sauce".to_string(),
            },
            TabInfo {
                id: TabId::new("t6"),
                title: "Best spaghetti carbonara recipe".to_string(),
                url: "https://recipes.com/carbonara".to_string(),
            },
        ];

        let clusters = cluster_tabs(&tabs, 0.75);
        // Should produce at least 2 clusters (tech + cooking)
        assert!(
            clusters.len() >= 2,
            "Expected at least 2 clusters, got {}",
            clusters.len()
        );
        // Each cluster should have at least 2 members
        for cluster in &clusters {
            assert!(cluster.tab_ids.len() >= 2);
        }
    }

    /// Dissimilar tabs at a reasonable threshold should not cluster.
    #[test]
    #[ignore]
    fn test_cluster_dissimilar_tabs_no_clusters() {
        let tabs = vec![
            TabInfo {
                id: TabId::new("t1"),
                title: "NASA Mars rover mission update 2026".to_string(),
                url: "https://nasa.gov/mars".to_string(),
            },
            TabInfo {
                id: TabId::new("t2"),
                title: "Learn to play guitar for beginners".to_string(),
                url: "https://guitartricks.com/beginner".to_string(),
            },
            TabInfo {
                id: TabId::new("t3"),
                title: "Property tax calculator for Texas".to_string(),
                url: "https://tax.texas.gov/calculator".to_string(),
            },
        ];
        let clusters = cluster_tabs(&tabs, 0.75);
        assert!(
            clusters.is_empty(),
            "Expected no clusters for dissimilar tabs, got {}",
            clusters.len()
        );
    }
}
