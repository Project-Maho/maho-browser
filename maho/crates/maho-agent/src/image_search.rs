// Copyright 2026 Maho Browser. All rights reserved.

//! Image search query and result contracts with entitlement tier routing.
//!
//! Provides bounded image metadata results with provenance.
//! Invariant: raw image binary is NEVER injected into model context (URLs/metadata only).
//! Integrates with the entitlement model shared with `deep_research.rs`.

use async_trait::async_trait;
use serde::{Deserialize, Serialize};
use std::sync::Arc;
use std::time::{SystemTime, UNIX_EPOCH};

pub use crate::web_search::{
    SearchEntitlement, SearchTier, DEFAULT_MAX_RESULTS, MAX_RESULTS_HARD_CAP,
};

/// Request parameters for image search.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ImageSearchQuery {
    pub query: String,
    pub max_results: usize,
    pub safe_search: bool,
    pub tier: SearchTier,
}

impl ImageSearchQuery {
    pub fn new(query: impl Into<String>) -> Self {
        Self {
            query: query.into(),
            max_results: DEFAULT_MAX_RESULTS,
            safe_search: true,
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

    pub fn with_safe_search(mut self, safe: bool) -> Self {
        self.safe_search = safe;
        self
    }

    /// Validates and normalizes parameters.
    /// Returns `Err(ImageSearchError::EmptyQuery)` if query is blank.
    /// Caps `max_results` between 1 and `MAX_RESULTS_HARD_CAP` (defaulting 0 to 10).
    pub fn validate_and_normalize(&mut self) -> Result<(), ImageSearchError> {
        let trimmed = self.query.trim();
        if trimmed.is_empty() {
            return Err(ImageSearchError::EmptyQuery);
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

/// Image search result item containing metadata and URLs ONLY.
///
/// Invariant: This struct contains NO raw binary buffers (`Vec<u8>`).
/// Model context only receives URLs and lightweight dimensional/provenance metadata.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ImageSearchResult {
    pub image_url: String,
    pub thumbnail_url: Option<String>,
    pub title: String,
    pub source_page_url: String,
    pub width: Option<u32>,
    pub height: Option<u32>,
    pub source: String,
    pub retrieved_at: u64,
}

/// Provenance metadata tracking image search origin.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ImageSearchProvenance {
    pub engine: String,
    pub source: String,
    pub tier: SearchTier,
    pub query_timestamp: u64,
}

/// Bounded image search response.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ImageSearchResponse {
    pub query: String,
    pub results: Vec<ImageSearchResult>,
    pub provenance: ImageSearchProvenance,
}

#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum ImageSearchError {
    #[error("search query cannot be empty")]
    EmptyQuery,
    #[error("search tier unavailable: {0}")]
    TypedUnavailable(String),
    #[error("search backend error: {0}")]
    BackendError(String),
}

/// Trait defining an image search client backend.
#[async_trait]
pub trait ImageSearchClient: Send + Sync {
    async fn search(
        &self,
        query: &ImageSearchQuery,
    ) -> Result<ImageSearchResponse, ImageSearchError>;
    fn backend_name(&self) -> &'static str;
    fn tier(&self) -> SearchTier;
}

pub use ImageSearchClient as ImageSearch;
pub use ImageSearchEntitlementRouter as ImageEntitlementRouter;

fn now_epoch_secs() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs()
}

/// Canned Free image search backend.
#[derive(Debug, Default, Clone)]
pub struct FreeImageSearchBackend;

#[async_trait]
impl ImageSearchClient for FreeImageSearchBackend {
    async fn search(
        &self,
        query: &ImageSearchQuery,
    ) -> Result<ImageSearchResponse, ImageSearchError> {
        let now = now_epoch_secs();
        let limit = query.max_results.clamp(1, MAX_RESULTS_HARD_CAP);
        let mut results = Vec::with_capacity(limit);

        for i in 1..=limit {
            results.push(ImageSearchResult {
                image_url: format!("https://images-free.example.com/{}-{}.jpg", query.query, i),
                thumbnail_url: Some(format!(
                    "https://thumbs-free.example.com/{}-{}-thumb.jpg",
                    query.query, i
                )),
                title: format!("Free Image {} for '{}'", i, query.query),
                source_page_url: format!("https://example-free.org/gallery/{}", i),
                width: Some(800),
                height: Some(600),
                source: "free_image_index".to_string(),
                retrieved_at: now,
            });
        }

        Ok(ImageSearchResponse {
            query: query.query.clone(),
            results,
            provenance: ImageSearchProvenance {
                engine: "free_image_index".to_string(),
                source: "free_image_index".to_string(),
                tier: SearchTier::Free,
                query_timestamp: now,
            },
        })
    }

    fn backend_name(&self) -> &'static str {
        "free_image_index"
    }

    fn tier(&self) -> SearchTier {
        SearchTier::Free
    }
}

/// Canned Premium image search backend.
#[derive(Debug, Default, Clone)]
pub struct PremiumImageSearchBackend;

#[async_trait]
impl ImageSearchClient for PremiumImageSearchBackend {
    async fn search(
        &self,
        query: &ImageSearchQuery,
    ) -> Result<ImageSearchResponse, ImageSearchError> {
        let now = now_epoch_secs();
        let limit = query.max_results.clamp(1, MAX_RESULTS_HARD_CAP);
        let mut results = Vec::with_capacity(limit);

        for i in 1..=limit {
            results.push(ImageSearchResult {
                image_url: format!(
                    "https://premium-cdn.example.com/highres/{}-{}.png",
                    query.query, i
                ),
                thumbnail_url: Some(format!(
                    "https://premium-cdn.example.com/thumb/{}-{}.webp",
                    query.query, i
                )),
                title: format!("High-Res Premium Image {} for '{}'", i, query.query),
                source_page_url: format!("https://premium-art.com/work/{}", i),
                width: Some(3840),
                height: Some(2160),
                source: "premium_visual_index".to_string(),
                retrieved_at: now,
            });
        }

        Ok(ImageSearchResponse {
            query: query.query.clone(),
            results,
            provenance: ImageSearchProvenance {
                engine: "premium_visual_index".to_string(),
                source: "premium_visual_index".to_string(),
                tier: SearchTier::Premium,
                query_timestamp: now,
            },
        })
    }

    fn backend_name(&self) -> &'static str {
        "premium_visual_index"
    }

    fn tier(&self) -> SearchTier {
        SearchTier::Premium
    }
}

/// Entitlement router for image search queries.
#[derive(Clone)]
pub struct ImageSearchEntitlementRouter {
    free_backend: Arc<dyn ImageSearchClient>,
    premium_backend: Arc<dyn ImageSearchClient>,
}

impl Default for ImageSearchEntitlementRouter {
    fn default() -> Self {
        Self {
            free_backend: Arc::new(FreeImageSearchBackend),
            premium_backend: Arc::new(PremiumImageSearchBackend),
        }
    }
}

impl ImageSearchEntitlementRouter {
    pub fn new(
        free_backend: Arc<dyn ImageSearchClient>,
        premium_backend: Arc<dyn ImageSearchClient>,
    ) -> Self {
        Self {
            free_backend,
            premium_backend,
        }
    }

    /// Selects the image backend client based on user entitlement.
    ///
    /// Invariant: Unentitled users can NEVER silently access the premium backend.
    /// If premium is explicitly requested without entitlement, returns `TypedUnavailable`.
    pub fn select_backend(
        &self,
        entitlement: &SearchEntitlement,
        requested_tier: Option<SearchTier>,
    ) -> Result<Arc<dyn ImageSearchClient>, ImageSearchError> {
        match requested_tier {
            Some(SearchTier::Premium) | Some(SearchTier::Enterprise) => {
                if entitlement.allows_premium() {
                    Ok(self.premium_backend.clone())
                } else {
                    Err(ImageSearchError::TypedUnavailable(
                        "Premium image search requires Max tier or active PAYG credits".to_string(),
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
        mut query: ImageSearchQuery,
    ) -> Result<ImageSearchResponse, ImageSearchError> {
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
        let router = ImageSearchEntitlementRouter::default();
        let max_user = SearchEntitlement::max();
        let query = ImageSearchQuery::new("nebula landscape").with_tier(SearchTier::Premium);

        let res = router.execute_search(&max_user, query).await.unwrap();
        assert_eq!(res.provenance.tier, SearchTier::Premium);
        assert_eq!(res.provenance.engine, "premium_visual_index");
        assert!(!res.results.is_empty());
        assert_eq!(res.results[0].source, "premium_visual_index");
        assert_eq!(res.results[0].width, Some(3840));
    }

    #[tokio::test]
    async fn test_tier_routing_payg_entitled_premium_selects_premium() {
        let router = ImageSearchEntitlementRouter::default();
        let payg_user = SearchEntitlement::payg(10.0);
        let query = ImageSearchQuery::new("architecture blueprint").with_tier(SearchTier::Premium);

        let res = router.execute_search(&payg_user, query).await.unwrap();
        assert_eq!(res.provenance.tier, SearchTier::Premium);
        assert_eq!(res.provenance.engine, "premium_visual_index");
    }

    #[tokio::test]
    async fn test_tier_routing_unentitled_premium_returns_typed_unavailable() {
        let router = ImageSearchEntitlementRouter::default();
        let free_user = SearchEntitlement::free();
        let query = ImageSearchQuery::new("high res photo").with_tier(SearchTier::Premium);

        let err = router.execute_search(&free_user, query).await.unwrap_err();
        match err {
            ImageSearchError::TypedUnavailable(msg) => {
                assert!(msg.contains("Premium image search requires Max tier"));
            }
            other => panic!("expected TypedUnavailable error, got: {:?}", other),
        }
    }

    #[tokio::test]
    async fn test_tier_routing_unentitled_free_selects_free_backend() {
        let router = ImageSearchEntitlementRouter::default();
        let free_user = SearchEntitlement::free();
        let query = ImageSearchQuery::new("cat photo").with_tier(SearchTier::Free);

        let res = router.execute_search(&free_user, query).await.unwrap();
        assert_eq!(res.provenance.tier, SearchTier::Free);
        assert_eq!(res.provenance.engine, "free_image_index");
        assert_eq!(res.results[0].source, "free_image_index");
    }

    #[tokio::test]
    async fn test_empty_query_returns_typed_error() {
        let router = ImageSearchEntitlementRouter::default();
        let free_user = SearchEntitlement::free();

        // completely empty
        let err1 = router
            .execute_search(&free_user, ImageSearchQuery::new(""))
            .await
            .unwrap_err();
        assert_eq!(err1, ImageSearchError::EmptyQuery);

        // whitespace only
        let err2 = router
            .execute_search(&free_user, ImageSearchQuery::new("   \t  \n"))
            .await
            .unwrap_err();
        assert_eq!(err2, ImageSearchError::EmptyQuery);
    }

    #[tokio::test]
    async fn test_results_bounded_default_10_and_enforced() {
        let router = ImageSearchEntitlementRouter::default();
        let free_user = SearchEntitlement::free();

        // Default max_results is 10
        let default_query = ImageSearchQuery::new("scenery");
        let res = router
            .execute_search(&free_user, default_query)
            .await
            .unwrap();
        assert_eq!(res.results.len(), 10);

        // Custom max_results = 4
        let custom_query = ImageSearchQuery::new("scenery").with_max_results(4);
        let res2 = router
            .execute_search(&free_user, custom_query)
            .await
            .unwrap();
        assert_eq!(res2.results.len(), 4);

        // Clamped to hard cap if excessive
        let oversized = ImageSearchQuery::new("scenery").with_max_results(5000);
        let res3 = router.execute_search(&free_user, oversized).await.unwrap();
        assert_eq!(res3.results.len(), MAX_RESULTS_HARD_CAP);
    }

    #[tokio::test]
    async fn test_provenance_and_url_metadata_only_no_binary_bytes() {
        let router = ImageSearchEntitlementRouter::default();
        let max_user = SearchEntitlement::max();
        let query = ImageSearchQuery::new("diagram")
            .with_tier(SearchTier::Premium)
            .with_max_results(3);

        let res = router.execute_search(&max_user, query).await.unwrap();
        assert_eq!(res.results.len(), 3);

        for item in &res.results {
            // Provenance assertions
            assert!(!item.image_url.is_empty(), "image_url must be populated");
            assert!(
                !item.source_page_url.is_empty(),
                "source_page_url must be populated"
            );
            assert!(!item.source.is_empty(), "source must be populated");
            assert!(item.retrieved_at > 0, "retrieved_at must be positive");

            // URL format checks
            assert!(item.image_url.starts_with("https://"));
            assert!(item.source_page_url.starts_with("https://"));

            // Serialized representation must only contain string/number fields, NEVER base64 or raw byte arrays
            let serialized = serde_json::to_value(item).unwrap();
            let obj = serialized.as_object().expect("result must be JSON object");

            for (key, val) in obj {
                assert!(
                    val.is_string() || val.is_number() || val.is_null(),
                    "Field '{}' has non-metadata JSON type (arrays/objects forbidden): {:?}",
                    key,
                    val
                );
            }
        }
    }
}
