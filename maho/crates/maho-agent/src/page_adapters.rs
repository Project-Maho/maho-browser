// Copyright 2026 Maho Browser. All rights reserved.

//! Exact-origin page adapter registry and fallback tier support.
//!
//! Provides specialized, origin-bounded adapters for difficult web surfaces
//! (such as Google Docs canvas, complex React trees, or virtualized tables)
//! with strict exact-origin matching and insertion-order determinism.

use serde::{Deserialize, Serialize};
use std::sync::Arc;

/// Canonicalizes a URL or origin string to its standard `scheme://host[:port]` representation.
///
/// Returns `None` if the input is not a valid HTTP/HTTPS URL or origin.
pub fn canonical_origin(url_or_origin: &str) -> Option<String> {
    let parsed = reqwest::Url::parse(url_or_origin).ok()?;
    let scheme = parsed.scheme();
    if scheme != "http" && scheme != "https" {
        return None;
    }
    let host = parsed.host_str()?;
    let origin = match parsed.port() {
        Some(port) => format!("{scheme}://{host}:{port}"),
        None => format!("{scheme}://{host}"),
    };
    Some(origin)
}

/// Checks whether a given origin or URL strictly matches an exact-origin pattern.
///
/// Substring/suffix matches (e.g. `docs.google.com.evil.io`), scheme mismatches,
/// and port mismatches fail closed and return `false`.
pub fn matches_exact_origin(pattern: &str, candidate: &str) -> bool {
    match (canonical_origin(pattern), canonical_origin(candidate)) {
        (Some(pat), Some(cand)) => pat == cand,
        _ => false,
    }
}

/// Metadata descriptor for a registered page adapter.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct PageAdapterDescriptor {
    pub adapter_id: String,
    pub exact_origin_patterns: Vec<String>,
    pub supported_operations: Vec<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub execution_world: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub max_output_bytes: Option<usize>,
}

/// Error type for page adapter operations.
#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum PageAdapterError {
    #[error("Invalid or non-HTTP(S) origin: {0}")]
    InvalidOrigin(String),
    #[error("Adapter '{adapter_id}' does not support operation '{operation}'")]
    UnsupportedOperation {
        adapter_id: String,
        operation: String,
    },
    #[error("Adapter '{0}' execution failed: {1}")]
    ExecutionFailed(String, String),
    #[error("No page adapter found for origin '{origin}' and operation '{operation}'")]
    NotFound { origin: String, operation: String },
}

/// Trait implemented by exact-origin page adapters.
pub trait PageAdapter: Send + Sync + std::fmt::Debug {
    /// Unique identifier for this adapter.
    fn id(&self) -> &str;

    /// List of exact origin patterns (e.g. `["https://docs.google.com"]`) supported by this adapter.
    fn exact_origins(&self) -> &[String];

    /// List of operation names (e.g. `["get_selection", "insert_text"]`) supported by this adapter.
    fn supported_operations(&self) -> &[String];

    /// Checks if this adapter matches the given origin or URL.
    fn matches_origin(&self, origin_or_url: &str) -> bool {
        let Some(target) = canonical_origin(origin_or_url) else {
            return false;
        };
        self.exact_origins()
            .iter()
            .any(|pattern| canonical_origin(pattern).map_or(false, |pat| pat == target))
    }

    /// Checks if this adapter supports the given operation name.
    fn supports_operation(&self, operation: &str) -> bool {
        self.supported_operations().iter().any(|op| op == operation)
    }

    /// Returns the serializable descriptor for this adapter.
    fn descriptor(&self) -> PageAdapterDescriptor {
        PageAdapterDescriptor {
            adapter_id: self.id().to_string(),
            exact_origin_patterns: self.exact_origins().to_vec(),
            supported_operations: self.supported_operations().to_vec(),
            execution_world: None,
            max_output_bytes: None,
        }
    }
}

/// A simple descriptor-backed page adapter.
#[derive(Debug, Clone)]
pub struct SimplePageAdapter {
    pub descriptor: PageAdapterDescriptor,
}

impl SimplePageAdapter {
    pub fn new(
        id: impl Into<String>,
        exact_origins: Vec<String>,
        supported_ops: Vec<String>,
    ) -> Self {
        Self {
            descriptor: PageAdapterDescriptor {
                adapter_id: id.into(),
                exact_origin_patterns: exact_origins,
                supported_operations: supported_ops,
                execution_world: None,
                max_output_bytes: None,
            },
        }
    }
}

impl PageAdapter for SimplePageAdapter {
    fn id(&self) -> &str {
        &self.descriptor.adapter_id
    }

    fn exact_origins(&self) -> &[String] {
        &self.descriptor.exact_origin_patterns
    }

    fn supported_operations(&self) -> &[String] {
        &self.descriptor.supported_operations
    }

    fn descriptor(&self) -> PageAdapterDescriptor {
        self.descriptor.clone()
    }
}

/// Registry holding exact-origin page adapters with insertion-order determinism.
#[derive(Debug, Default, Clone)]
pub struct PageAdapterRegistry {
    adapters: Vec<Arc<dyn PageAdapter>>,
}

impl PageAdapterRegistry {
    /// Creates a new empty registry.
    pub fn new() -> Self {
        Self {
            adapters: Vec::new(),
        }
    }

    /// Registers an adapter instance, preserving insertion order.
    pub fn register<A: PageAdapter + 'static>(&mut self, adapter: A) {
        self.adapters.push(Arc::new(adapter));
    }

    /// Registers a shared adapter instance, preserving insertion order.
    pub fn register_arc(&mut self, adapter: Arc<dyn PageAdapter>) {
        self.adapters.push(adapter);
    }

    /// Registers a descriptor-backed adapter.
    pub fn register_descriptor(&mut self, descriptor: PageAdapterDescriptor) {
        self.adapters
            .push(Arc::new(SimplePageAdapter { descriptor }));
    }

    /// Convenience method to register a simple adapter by parts.
    pub fn register_simple(
        &mut self,
        id: impl Into<String>,
        exact_origins: Vec<String>,
        supported_ops: Vec<String>,
    ) {
        self.register(SimplePageAdapter::new(id, exact_origins, supported_ops));
    }

    /// Looks up the first matching adapter for `(origin_or_url, operation)` in insertion order.
    ///
    /// Returns `None` if origin does not match or operation is unsupported.
    pub fn lookup(&self, origin_or_url: &str, operation: &str) -> Option<Arc<dyn PageAdapter>> {
        let target_origin = canonical_origin(origin_or_url)?;
        for adapter in &self.adapters {
            let origin_matches = adapter
                .exact_origins()
                .iter()
                .any(|pattern| canonical_origin(pattern).map_or(false, |pat| pat == target_origin));
            if origin_matches && adapter.supports_operation(operation) {
                return Some(Arc::clone(adapter));
            }
        }
        None
    }

    /// Alias for lookup.
    pub fn find_adapter(
        &self,
        origin_or_url: &str,
        operation: &str,
    ) -> Option<Arc<dyn PageAdapter>> {
        self.lookup(origin_or_url, operation)
    }

    /// Alias for lookup.
    pub fn resolve(&self, origin_or_url: &str, operation: &str) -> Option<Arc<dyn PageAdapter>> {
        self.lookup(origin_or_url, operation)
    }

    /// Returns a slice of all registered adapters in insertion order.
    pub fn adapters(&self) -> &[Arc<dyn PageAdapter>] {
        &self.adapters
    }

    /// Returns the number of registered adapters.
    pub fn len(&self) -> usize {
        self.adapters.len()
    }

    /// Returns true if no adapters are registered.
    pub fn is_empty(&self) -> bool {
        self.adapters.is_empty()
    }

    /// Clears all registered adapters.
    pub fn clear(&mut self) {
        self.adapters.clear();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_canonical_origin_parsing() {
        assert_eq!(
            canonical_origin("https://docs.google.com/document/d/123/edit"),
            Some("https://docs.google.com".to_string())
        );
        assert_eq!(
            canonical_origin("https://docs.google.com"),
            Some("https://docs.google.com".to_string())
        );
        assert_eq!(
            canonical_origin("https://docs.google.com:8443/test"),
            Some("https://docs.google.com:8443".to_string())
        );
        assert_eq!(
            canonical_origin("http://insecure.local"),
            Some("http://insecure.local".to_string())
        );
        assert_eq!(canonical_origin("ftp://invalid.scheme"), None);
        assert_eq!(canonical_origin("not a url"), None);
    }

    #[test]
    fn test_matches_exact_origin_probes() {
        assert!(matches_exact_origin(
            "https://docs.google.com",
            "https://docs.google.com/doc/1"
        ));
        assert!(!matches_exact_origin(
            "https://docs.google.com",
            "https://docs.google.com.evil.io"
        ));
        assert!(!matches_exact_origin(
            "https://docs.google.com",
            "http://docs.google.com"
        ));
    }
}
