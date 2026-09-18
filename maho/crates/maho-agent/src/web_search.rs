// Copyright 2026 Maho Browser. All rights reserved.

//! Web search query and result contracts with entitlement tier routing.
//!
//! Provides bounded search result schemas carrying full origin provenance.
//! Integrates with the entitlement model shared with `deep_research.rs`.

use async_trait::async_trait;
use maho_types::account::UserTier;
use serde::{Deserialize, Serialize};
use std::sync::Arc;
use std::time::{SystemTime, UNIX_EPOCH};

pub const DEFAULT_MAX_RESULTS: usize = 10;
pub const MAX_RESULTS_HARD_CAP: usize = 50;

/// Service tier determining routing and feature availability.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SearchTier {
    Free,
    Premium,
    Enterprise,
}

/// Request parameters for executing a web search query.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct WebSearchQuery {
    pub query: String,
    pub max_results: usize,
    pub freshness_days: Option<u32>,
    pub tier: SearchTier,
}

impl WebSearchQuery {
    pub fn new(query: impl Into<String>) -> Self {
        Self {
            query: query.into(),
            max_results: DEFAULT_MAX_RESULTS,
            freshness_days: None,
            tier: SearchTier::Free,
        }
    }

    pub fn with_max_results(mut self, max: usize) -> Self {
        self.max_results = max;
        self
    }

    pub fn with_tier(mut self, tier: SearchTier) -> Self {
        self.tier = tier;
        self
    }

    pub fn with_freshness(mut self, freshness_days: u32) -> Self {
        self.freshness_days = Some(freshness_days);
        self
    }

    /// Validates and normalizes parameters.
    /// Returns `Err(WebSearchError::EmptyQuery)` if query is blank.
    /// Caps `max_results` between 1 and `MAX_RESULTS_HARD_CAP` (defaulting 0 to 10).
    pub fn validate_and_normalize(&mut self) -> Result<(), WebSearchError> {
        let trimmed = self.query.trim();
        if trimmed.is_empty() {
            return Err(WebSearchError::EmptyQuery);
        }
        self.query = trimmed.to_string();

        if self.max_results == 0 {
            self.max_results = DEFAULT_MAX_RESULTS;
        } else if self.max_results > MAX_RESULTS_HARD_CAP {
            self.max_results = MAX_RESULTS_HARD_CAP;
        }

        Ok(())
    }
}

/// A single bounded search result item carrying provenance.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct WebSearchResult {
    pub title: String,
    pub url: String,
    pub snippet: String,
    pub published_date: Option<String>,
    pub score: Option<f32>,
    pub source: String,
    pub retrieved_at: u64,
}

/// Provenance metadata tracking search origin and caching.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct WebSearchProvenance {
    pub engine: String,
    pub source: String,
    pub tier: SearchTier,
    pub query_timestamp: u64,
    pub cached: bool,
}

/// Complete response containing bounded results and provenance.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct WebSearchResponse {
    pub query: String,
    pub results: Vec<WebSearchResult>,
    pub provenance: WebSearchProvenance,
}

#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum WebSearchError {
    #[error("search query cannot be empty")]
    EmptyQuery,
    #[error("search tier unavailable: {0}")]
    TypedUnavailable(String),
    #[error("search backend error: {0}")]
    BackendError(String),
}

/// What a user is entitled to for search.
/// Matches the entitlement model used in `deep_research.rs`.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct SearchEntitlement {
    pub tier: UserTier,
    pub payg_credits_usd: f64,
}

impl SearchEntitlement {
    pub fn new(tier: UserTier, payg_credits_usd: f64) -> Self {
        Self {
            tier,
            payg_credits_usd,
        }
    }

    pub fn free() -> Self {
        Self {
            tier: UserTier::Free,
            payg_credits_usd: 0.0,
        }
    }

    pub fn max() -> Self {
        Self {
            tier: UserTier::Max,
            payg_credits_usd: 0.0,
        }
    }

    pub fn payg(credits_usd: f64) -> Self {
        Self {
            tier: UserTier::Free,
            payg_credits_usd: credits_usd,
        }
    }

    pub fn allows_premium(&self) -> bool {
        matches!(self.tier, UserTier::Max) || self.payg_credits_usd > 0.0
    }
}

impl From<crate::deep_research::ResearchEntitlement> for SearchEntitlement {
    fn from(entitlement: crate::deep_research::ResearchEntitlement) -> Self {
        Self {
            tier: entitlement.tier,
            payg_credits_usd: entitlement.payg_credits_usd,
        }
    }
}

impl From<UserTier> for SearchEntitlement {
    fn from(tier: UserTier) -> Self {
        Self {
            tier,
            payg_credits_usd: 0.0,
        }
    }
}

/// Trait defining a web search client backend.
#[async_trait]
pub trait WebSearchClient: Send + Sync {
    async fn search(&self, query: &WebSearchQuery) -> Result<WebSearchResponse, WebSearchError>;
    fn backend_name(&self) -> &'static str;
    fn tier(&self) -> SearchTier;
}

pub use WebSearchClient as SearchClient;
pub use WebSearchEntitlementRouter as EntitlementRouter;

fn now_epoch_secs() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs()
}

/// Canned Free backend returning bounded fixtures.
#[derive(Debug, Default, Clone)]
pub struct FreeWebSearchBackend;

#[async_trait]
impl WebSearchClient for FreeWebSearchBackend {
    async fn search(&self, query: &WebSearchQuery) -> Result<WebSearchResponse, WebSearchError> {
        let now = now_epoch_secs();
        let limit = query.max_results.clamp(1, MAX_RESULTS_HARD_CAP);
        let mut results = Vec::with_capacity(limit);

        for i in 1..=limit {
            results.push(WebSearchResult {
                title: format!("Free Result {} for '{}'", i, query.query),
                url: format!("https://example-free.org/page-{}", i),
                snippet: format!(
                    "Free index snippet summary for '{}' item {}.",
                    query.query, i
                ),
                published_date: Some("2026-08-01".to_string()),
                score: Some(1.0 / (i as f32)),
                source: "free_web_index".to_string(),
                retrieved_at: now,
            });
        }

        Ok(WebSearchResponse {
            query: query.query.clone(),
            results,
            provenance: WebSearchProvenance {
                engine: "free_web_index".to_string(),
                source: "free_web_index".to_string(),
                tier: SearchTier::Free,
                query_timestamp: now,
                cached: false,
            },
        })
    }

    fn backend_name(&self) -> &'static str {
        "free_web_index"
    }

    fn tier(&self) -> SearchTier {
        SearchTier::Free
    }
}

/// Canned Premium backend returning rich bounded fixtures.
#[derive(Debug, Default, Clone)]
pub struct PremiumWebSearchBackend;

#[async_trait]
impl WebSearchClient for PremiumWebSearchBackend {
    async fn search(&self, query: &WebSearchQuery) -> Result<WebSearchResponse, WebSearchError> {
        let now = now_epoch_secs();
        let limit = query.max_results.clamp(1, MAX_RESULTS_HARD_CAP);
        let mut results = Vec::with_capacity(limit);

        for i in 1..=limit {
            results.push(WebSearchResult {
                title: format!("Premium Result {} for '{}'", i, query.query),
                url: format!("https://example-premium.com/article-{}", i),
                snippet: format!(
                    "High-authority premium neural snippet for '{}' item {}.",
                    query.query, i
                ),
                published_date: Some("2026-08-20".to_string()),
                score: Some(0.99 - (i as f32 * 0.05)),
                source: "premium_neural_index".to_string(),
                retrieved_at: now,
            });
        }

        Ok(WebSearchResponse {
            query: query.query.clone(),
            results,
            provenance: WebSearchProvenance {
                engine: "premium_neural_index".to_string(),
                source: "premium_neural_index".to_string(),
                tier: SearchTier::Premium,
                query_timestamp: now,
                cached: false,
            },
        })
    }

    fn backend_name(&self) -> &'static str {
        "premium_neural_index"
    }

    fn tier(&self) -> SearchTier {
        SearchTier::Premium
    }
}

/// Entitlement router that selects the search backend based on user tier and requested features.
#[derive(Clone)]
pub struct WebSearchEntitlementRouter {
    free_backend: Arc<dyn WebSearchClient>,
    premium_backend: Arc<dyn WebSearchClient>,
}

impl Default for WebSearchEntitlementRouter {
    fn default() -> Self {
        Self {
            free_backend: Arc::new(FreeWebSearchBackend),
            premium_backend: Arc::new(PremiumWebSearchBackend),
        }
    }
}

impl WebSearchEntitlementRouter {
    pub fn new(
        free_backend: Arc<dyn WebSearchClient>,
        premium_backend: Arc<dyn WebSearchClient>,
    ) -> Self {
        Self {
            free_backend,
            premium_backend,
        }
    }

    /// Selects the backend client based on the entitlement and requested tier.
    ///
    /// Invariant: Unentitled users can NEVER silently access the premium backend.
    /// If premium is explicitly requested without entitlement, returns `TypedUnavailable`.
    pub fn select_backend(
        &self,
        entitlement: &SearchEntitlement,
        requested_tier: Option<SearchTier>,
    ) -> Result<Arc<dyn WebSearchClient>, WebSearchError> {
        match requested_tier {
            Some(SearchTier::Premium) | Some(SearchTier::Enterprise) => {
                if entitlement.allows_premium() {
                    Ok(self.premium_backend.clone())
                } else {
                    Err(WebSearchError::TypedUnavailable(
                        "Premium web search requires Max tier or active PAYG credits".to_string(),
                    ))
                }
            }
            Some(SearchTier::Free) => Ok(self.free_backend.clone()),
            None => {
                if entitlement.allows_premium() {
                    Ok(self.premium_backend.clone())
                } else {
                    Ok(self.free_backend.clone())
                }
            }
        }
    }

    /// Validates query, applies bounds, routes backend, and ensures provenance on every result.
    pub async fn execute_search(
        &self,
        entitlement: &SearchEntitlement,
        mut query: WebSearchQuery,
    ) -> Result<WebSearchResponse, WebSearchError> {
        query.validate_and_normalize()?;

        let client = self.select_backend(entitlement, Some(query.tier))?;
        let mut response = client.search(&query).await?;

        // Enforce upper bound on result length
        if response.results.len() > query.max_results {
            response.results.truncate(query.max_results);
        }

        // Assert provenance invariants on all items
        for item in &mut response.results {
            if item.source.is_empty() {
                item.source = response.provenance.source.clone();
            }
            if item.retrieved_at == 0 {
                item.retrieved_at = response.provenance.query_timestamp;
            }
        }

        Ok(response)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn test_tier_routing_entitled_premium_selects_premium_backend() {
        let router = WebSearchEntitlementRouter::default();
        let max_user = SearchEntitlement::max();
        let query = WebSearchQuery::new("quantum computing").with_tier(SearchTier::Premium);

        let res = router.execute_search(&max_user, query).await.unwrap();
        assert_eq!(res.provenance.tier, SearchTier::Premium);
        assert_eq!(res.provenance.engine, "premium_neural_index");
        assert!(!res.results.is_empty());
        assert_eq!(res.results[0].source, "premium_neural_index");
    }

    #[tokio::test]
    async fn test_tier_routing_payg_entitled_premium_selects_premium() {
        let router = WebSearchEntitlementRouter::default();
        let payg_user = SearchEntitlement::payg(5.0);
        let query = WebSearchQuery::new("rust async").with_tier(SearchTier::Premium);

        let res = router.execute_search(&payg_user, query).await.unwrap();
        assert_eq!(res.provenance.tier, SearchTier::Premium);
        assert_eq!(res.provenance.engine, "premium_neural_index");
    }

    #[tokio::test]
    async fn test_tier_routing_unentitled_premium_returns_typed_unavailable() {
        let router = WebSearchEntitlementRouter::default();
        let free_user = SearchEntitlement::free();
        let query = WebSearchQuery::new("quantum computing").with_tier(SearchTier::Premium);

        let err = router.execute_search(&free_user, query).await.unwrap_err();
        match err {
            WebSearchError::TypedUnavailable(msg) => {
                assert!(msg.contains("Premium web search requires Max tier"));
            }
            other => panic!("expected TypedUnavailable error, got: {:?}", other),
        }
    }

    #[tokio::test]
    async fn test_tier_routing_unentitled_free_selects_free_backend() {
        let router = WebSearchEntitlementRouter::default();
        let free_user = SearchEntitlement::free();
        let query = WebSearchQuery::new("rust programming").with_tier(SearchTier::Free);

        let res = router.execute_search(&free_user, query).await.unwrap();
        assert_eq!(res.provenance.tier, SearchTier::Free);
        assert_eq!(res.provenance.engine, "free_web_index");
        assert_eq!(res.results[0].source, "free_web_index");
    }

    #[tokio::test]
    async fn test_empty_query_returns_typed_error() {
        let router = WebSearchEntitlementRouter::default();
        let free_user = SearchEntitlement::free();

        // completely empty
        let err1 = router
            .execute_search(&free_user, WebSearchQuery::new(""))
            .await
            .unwrap_err();
        assert_eq!(err1, WebSearchError::EmptyQuery);

        // whitespace only
        let err2 = router
            .execute_search(&free_user, WebSearchQuery::new("   \n\t  "))
            .await
            .unwrap_err();
        assert_eq!(err2, WebSearchError::EmptyQuery);
    }

    #[tokio::test]
    async fn test_results_bounded_default_10_and_enforced() {
        let router = WebSearchEntitlementRouter::default();
        let free_user = SearchEntitlement::free();

        // Default max_results is 10
        let default_query = WebSearchQuery::new("test");
        let res = router
            .execute_search(&free_user, default_query)
            .await
            .unwrap();
        assert_eq!(res.results.len(), 10);

        // Custom max_results = 3
        let bounded_query = WebSearchQuery::new("test").with_max_results(3);
        let res2 = router
            .execute_search(&free_user, bounded_query)
            .await
            .unwrap();
        assert_eq!(res2.results.len(), 3);

        // Clamped to hard cap if excessive
        let oversized_query = WebSearchQuery::new("test").with_max_results(9999);
        let res3 = router
            .execute_search(&free_user, oversized_query)
            .await
            .unwrap();
        assert_eq!(res3.results.len(), MAX_RESULTS_HARD_CAP);
    }

    #[tokio::test]
    async fn test_provenance_fields_present_on_each_result() {
        let router = WebSearchEntitlementRouter::default();
        let max_user = SearchEntitlement::max();
        let query = WebSearchQuery::new("provenance check")
            .with_tier(SearchTier::Premium)
            .with_max_results(5);

        let res = router.execute_search(&max_user, query).await.unwrap();
        assert_eq!(res.results.len(), 5);

        for item in &res.results {
            assert!(!item.url.is_empty(), "result must carry non-empty url");
            assert!(
                !item.source.is_empty(),
                "result must carry non-empty source provenance"
            );
            assert!(
                item.retrieved_at > 0,
                "result must carry positive retrieved_at timestamp"
            );
            assert!(item.url.starts_with("https://"));
        }
    }
}
