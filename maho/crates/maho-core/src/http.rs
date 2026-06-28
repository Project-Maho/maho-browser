//! Shared HTTP client for the maho-core crate.
//!
//! Constructing a `reqwest::Client` reinitializes the connection pool,
//! TLS cache, and DNS resolver. Reusing a single client across all
//! call sites eliminates per-request handshake cost (audit finding H9).
//!
//! Consolidated config: no per-site timeouts or user-agent were previously
//! set (all sites used `Client::new()` or `Client::builder().build()`),
//! so the shared client uses reqwest defaults.

use std::sync::OnceLock;

/// Returns a shared `reqwest::Client`. Internally `Arc`-backed — clone is cheap.
pub(crate) fn shared_http_client() -> reqwest::Client {
    static CLIENT: OnceLock<reqwest::Client> = OnceLock::new();
    CLIENT
        .get_or_init(|| {
            reqwest::Client::builder()
                .build()
                .expect("reqwest::Client::builder().build() should not fail")
        })
        .clone()
}
