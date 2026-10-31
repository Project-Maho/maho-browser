//! Shared HTTP client for the maho-core crate.
//!
//! Constructing a `reqwest::Client` reinitializes the connection pool,
//! TLS cache, and DNS resolver. Reusing a single client across all
//! call sites eliminates per-request handshake cost (audit finding H9).
//!
//! Consolidated config: no per-site user-agent was previously set. The shared
//! client bounds connection establishment and idle pooled connections but sets
//! no total request timeout, because streaming (SSE) responses are long-lived;
//! stream consumers enforce their own idle timeouts.

use std::sync::OnceLock;
use std::time::Duration;

const CONNECT_TIMEOUT: Duration = Duration::from_secs(15);
const POOL_IDLE_TIMEOUT: Duration = Duration::from_secs(90);

/// Returns a shared `reqwest::Client`. Internally `Arc`-backed — clone is cheap.
pub(crate) fn shared_http_client() -> reqwest::Client {
    static CLIENT: OnceLock<reqwest::Client> = OnceLock::new();
    CLIENT
        .get_or_init(|| {
            reqwest::Client::builder()
                .connect_timeout(CONNECT_TIMEOUT)
                .pool_idle_timeout(POOL_IDLE_TIMEOUT)
                .build()
                .expect("reqwest::Client::builder().build() should not fail")
        })
        .clone()
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::net::TcpListener;
    use std::time::Instant;

    #[test]
    fn shared_client_builds_with_bounded_connect_and_pool_timeouts() {
        assert!(CONNECT_TIMEOUT <= Duration::from_secs(30));
        assert!(POOL_IDLE_TIMEOUT <= Duration::from_secs(120));
        // Constructing must not panic with the configured timeouts.
        let _ = shared_http_client();
        // Sanity: a refused connect fails fast instead of hanging.
        let port = TcpListener::bind("127.0.0.1:0")
            .unwrap()
            .local_addr()
            .unwrap()
            .port();
        let rt = tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
            .unwrap();
        let started = Instant::now();
        let result = rt.block_on(shared_http_client().get(format!("http://127.0.0.1:{port}/")).send());
        assert!(result.is_err());
        assert!(started.elapsed() < CONNECT_TIMEOUT);
    }
}
