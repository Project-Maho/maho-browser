use async_trait::async_trait;
use futures::{Stream, StreamExt};
use reqwest::Url;
use serde::Serialize;
use serde_json::Value;
use std::net::{IpAddr, SocketAddr};
use std::pin::Pin;
use std::sync::Arc;
use std::time::Duration;

pub const WEB_FETCH_MAX_BYTES: usize = 1024 * 1024;
pub const WEB_FETCH_TIMEOUT: Duration = Duration::from_secs(15);
const MAX_REDIRECTS: usize = 5;
const DEFAULT_WINDOW_CHARS: usize = 20_000;

type BodyStream = Pin<Box<dyn Stream<Item = Result<Vec<u8>, WebFetchError>> + Send + 'static>>;

#[derive(Debug, thiserror::Error)]
pub enum WebFetchError {
    #[error("Missing required parameter: url")]
    MissingUrl,
    #[error("Invalid URL: {0}")]
    InvalidUrl(String),
    #[error("URL scheme must be http or https, got: {0}")]
    UnsupportedScheme(String),
    #[error("URL must include a host")]
    MissingHost,
    #[error("URLs containing credentials are not allowed")]
    CredentialsNotAllowed,
    #[error("Network access to non-public address {0} is blocked")]
    BlockedAddress(IpAddr),
    #[error("Network access to local hostname {0} is blocked")]
    BlockedHostname(String),
    #[error("DNS resolution failed for {host}: {source}")]
    DnsResolution {
        host: String,
        #[source]
        source: std::io::Error,
    },
    #[error("DNS resolution returned no addresses for {0}")]
    NoAddresses(String),
    #[error("Failed to construct HTTP client: {0}")]
    Client(#[source] reqwest::Error),
    #[error("HTTP request failed: {0}")]
    Request(#[source] reqwest::Error),
    #[error("Invalid redirect location: {0}")]
    InvalidRedirect(String),
    #[error("HTTP redirect limit exceeded ({MAX_REDIRECTS})")]
    TooManyRedirects,
    #[error("Web fetch timed out after {0:?}")]
    Timeout(Duration),
}

#[derive(Debug, Serialize)]
pub struct WebFetchResult {
    pub url: String,
    pub status: u16,
    pub content_type: Option<String>,
    pub content: String,
    pub truncated: bool,
    pub offset_chars: usize,
}

struct TransportResponse {
    url: Url,
    status: u16,
    content_type: Option<String>,
    location: Option<String>,
    body: BodyStream,
}

#[async_trait]
trait WebFetchTransport: Send + Sync {
    async fn send(&self, url: Url) -> Result<TransportResponse, WebFetchError>;
}

struct ReqwestWebFetchTransport {
    timeout: Duration,
}

#[async_trait]
impl WebFetchTransport for ReqwestWebFetchTransport {
    async fn send(&self, url: Url) -> Result<TransportResponse, WebFetchError> {
        let resolved = resolve_public_addresses(&url).await?;
        let mut builder = reqwest::Client::builder()
            .no_proxy()
            .redirect(reqwest::redirect::Policy::none())
            .timeout(self.timeout)
            .user_agent("Maho-Agent/0.1");

        if let Some((host, addresses)) = resolved {
            builder = builder.resolve_to_addrs(&host, &addresses);
        }

        let client = builder.build().map_err(WebFetchError::Client)?;
        let response = client
            .get(url)
            .send()
            .await
            .map_err(WebFetchError::Request)?;

        if let Some(remote_addr) = response.remote_addr() {
            validate_public_ip(remote_addr.ip())?;
        }

        let response_url = response.url().clone();
        let status = response.status().as_u16();
        let content_type = response
            .headers()
            .get(reqwest::header::CONTENT_TYPE)
            .and_then(|value| value.to_str().ok())
            .map(str::to_string);
        let location = response
            .headers()
            .get(reqwest::header::LOCATION)
            .map(|value| {
                value
                    .to_str()
                    .map(str::to_string)
                    .map_err(|error| WebFetchError::InvalidRedirect(error.to_string()))
            })
            .transpose()?;
        let body = response.bytes_stream().map(|chunk| {
            chunk
                .map(|bytes| bytes.to_vec())
                .map_err(WebFetchError::Request)
        });

        Ok(TransportResponse {
            url: response_url,
            status,
            content_type,
            location,
            body: Box::pin(body),
        })
    }
}

pub struct WebFetchExecutor {
    transport: Arc<dyn WebFetchTransport>,
    timeout: Duration,
    max_bytes: usize,
}

impl WebFetchExecutor {
    pub fn new_default() -> Self {
        Self {
            transport: Arc::new(ReqwestWebFetchTransport {
                timeout: WEB_FETCH_TIMEOUT,
            }),
            timeout: WEB_FETCH_TIMEOUT,
            max_bytes: WEB_FETCH_MAX_BYTES,
        }
    }

    pub async fn execute(&self, args: Value) -> Result<Value, WebFetchError> {
        let raw_url = args
            .get("url")
            .and_then(Value::as_str)
            .ok_or(WebFetchError::MissingUrl)?;
        let url = validate_url(raw_url)?;
        let offset_chars = args
            .get("offset_chars")
            .and_then(Value::as_u64)
            .and_then(|value| usize::try_from(value).ok())
            .unwrap_or(0);
        let max_chars = args
            .get("max_chars")
            .and_then(Value::as_u64)
            .and_then(|value| usize::try_from(value).ok())
            .unwrap_or(DEFAULT_WINDOW_CHARS)
            .min(DEFAULT_WINDOW_CHARS);

        let result = tokio::time::timeout(self.timeout, self.fetch(url))
            .await
            .map_err(|_| WebFetchError::Timeout(self.timeout))??;
        let content: String = result
            .content
            .chars()
            .skip(offset_chars)
            .take(max_chars)
            .collect();
        let truncated = result.truncated
            || result.content.chars().count() > offset_chars.saturating_add(max_chars);
        Ok(serde_json::json!(WebFetchResult {
            content,
            truncated,
            offset_chars,
            ..result
        }))
    }

    async fn fetch(&self, mut url: Url) -> Result<WebFetchResult, WebFetchError> {
        for redirect_count in 0..=MAX_REDIRECTS {
            let mut response = self.transport.send(url.clone()).await?;
            if (300..400).contains(&response.status) {
                if let Some(location) = response.location.as_deref() {
                    if redirect_count == MAX_REDIRECTS {
                        return Err(WebFetchError::TooManyRedirects);
                    }
                    let redirected = response
                        .url
                        .join(location)
                        .map_err(|error| WebFetchError::InvalidRedirect(error.to_string()))?;
                    url = validate_parsed_url(redirected)?;
                    continue;
                }
            }

            let mut content = Vec::new();
            let mut truncated = false;
            while let Some(chunk) = response.body.next().await {
                let chunk = chunk?;
                let remaining = self.max_bytes.saturating_sub(content.len());
                if chunk.len() > remaining {
                    content.extend_from_slice(&chunk[..remaining]);
                    truncated = true;
                    break;
                }
                content.extend_from_slice(&chunk);
            }

            return Ok(WebFetchResult {
                url: response.url.to_string(),
                status: response.status,
                content_type: response.content_type,
                content: String::from_utf8_lossy(&content).into_owned(),
                truncated,
                offset_chars: 0,
            });
        }

        Err(WebFetchError::TooManyRedirects)
    }
}

pub fn validate_url(raw_url: &str) -> Result<Url, WebFetchError> {
    let url = Url::parse(raw_url).map_err(|error| WebFetchError::InvalidUrl(error.to_string()))?;
    validate_parsed_url(url)
}

fn validate_parsed_url(url: Url) -> Result<Url, WebFetchError> {
    if !matches!(url.scheme(), "http" | "https") {
        return Err(WebFetchError::UnsupportedScheme(url.scheme().to_string()));
    }
    if !url.username().is_empty() || url.password().is_some() {
        return Err(WebFetchError::CredentialsNotAllowed);
    }

    let host = url.host_str().ok_or(WebFetchError::MissingHost)?;
    if let Some(address) = parse_ip_host(host) {
        validate_public_ip(address)?;
    } else {
        let domain = host.trim_end_matches('.');
        if domain.eq_ignore_ascii_case("localhost")
            || domain.to_ascii_lowercase().ends_with(".localhost")
        {
            return Err(WebFetchError::BlockedHostname(domain.to_string()));
        }
    }
    Ok(url)
}

async fn resolve_public_addresses(
    url: &Url,
) -> Result<Option<(String, Vec<SocketAddr>)>, WebFetchError> {
    let host = url
        .host_str()
        .ok_or(WebFetchError::MissingHost)?
        .to_string();
    if parse_ip_host(&host).is_some() {
        return Ok(None);
    }
    let port = url
        .port_or_known_default()
        .ok_or(WebFetchError::MissingHost)?;
    let addresses: Vec<SocketAddr> = tokio::net::lookup_host((host.as_str(), port))
        .await
        .map_err(|source| WebFetchError::DnsResolution {
            host: host.clone(),
            source,
        })?
        .collect();
    if addresses.is_empty() {
        return Err(WebFetchError::NoAddresses(host));
    }
    for address in &addresses {
        validate_public_ip(address.ip())?;
    }
    Ok(Some((host, addresses)))
}

fn parse_ip_host(host: &str) -> Option<IpAddr> {
    host.trim_start_matches('[')
        .trim_end_matches(']')
        .parse()
        .ok()
}

fn validate_public_ip(address: IpAddr) -> Result<(), WebFetchError> {
    if is_non_public_ip(address) {
        Err(WebFetchError::BlockedAddress(address))
    } else {
        Ok(())
    }
}

fn is_non_public_ip(address: IpAddr) -> bool {
    match address {
        IpAddr::V4(address) => {
            let [a, b, c, _] = address.octets();
            a == 0
                || a == 10
                || a == 127
                || (a == 100 && (64..=127).contains(&b))
                || (a == 169 && b == 254)
                || (a == 172 && (16..=31).contains(&b))
                || (a == 192 && b == 0 && c == 0)
                || (a == 192 && b == 0 && c == 2)
                || (a == 192 && b == 168)
                || (a == 198 && (b == 18 || b == 19))
                || (a == 198 && b == 51 && c == 100)
                || (a == 203 && b == 0 && c == 113)
                || a >= 224
        }
        IpAddr::V6(address) => {
            if let Some(address) = address.to_ipv4() {
                return is_non_public_ip(IpAddr::V4(address));
            }
            let segments = address.segments();
            address.is_unspecified()
                || address.is_loopback()
                || (segments[0] & 0xfe00) == 0xfc00
                || (segments[0] & 0xffc0) == 0xfe80
                || (segments[0] & 0xff00) == 0xff00
                || (segments[0] == 0x2001 && segments[1] == 0x0db8)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures::stream;

    struct StaticTransport {
        chunks: Vec<Vec<u8>>,
    }

    #[async_trait]
    impl WebFetchTransport for StaticTransport {
        async fn send(&self, url: Url) -> Result<TransportResponse, WebFetchError> {
            let chunks = self.chunks.clone().into_iter().map(Ok);
            Ok(TransportResponse {
                url,
                status: 200,
                content_type: Some("text/plain".to_string()),
                location: None,
                body: Box::pin(stream::iter(chunks)),
            })
        }
    }

    struct PendingTransport;

    #[async_trait]
    impl WebFetchTransport for PendingTransport {
        async fn send(&self, _url: Url) -> Result<TransportResponse, WebFetchError> {
            std::future::pending().await
        }
    }

    fn test_executor(
        transport: Arc<dyn WebFetchTransport>,
        timeout: Duration,
        max_bytes: usize,
    ) -> WebFetchExecutor {
        WebFetchExecutor {
            transport,
            timeout,
            max_bytes,
        }
    }

    #[test]
    fn validate_url_accepts_public_http_and_https() {
        assert!(validate_url("https://example.com/path?q=weather").is_ok());
        assert!(validate_url("http://8.8.8.8/").is_ok());
    }

    #[test]
    fn validate_url_rejects_unsupported_schemes_and_non_public_targets() {
        assert!(matches!(
            validate_url("file:///etc/passwd"),
            Err(WebFetchError::UnsupportedScheme(_))
        ));
        assert!(matches!(
            validate_url("http://localhost:8080/"),
            Err(WebFetchError::BlockedHostname(_))
        ));
        for raw_url in [
            "http://127.0.0.1/",
            "http://10.0.0.1/",
            "http://169.254.169.254/",
            "http://192.168.1.1/",
            "http://[::1]/",
            "http://[fe80::1]/",
            "http://[fc00::1]/",
        ] {
            assert!(
                matches!(validate_url(raw_url), Err(WebFetchError::BlockedAddress(_))),
                "expected {raw_url} to be blocked"
            );
        }
    }

    #[tokio::test]
    async fn fetch_enforces_response_size_cap() {
        let executor = test_executor(
            Arc::new(StaticTransport {
                chunks: vec![b"1234".to_vec(), b"56789".to_vec()],
            }),
            Duration::from_secs(1),
            6,
        );

        let result = executor
            .execute(serde_json::json!({"url": "https://example.com/data"}))
            .await
            .expect("fetch succeeds");

        assert_eq!(result["content"], "123456");
        assert_eq!(result["truncated"], true);
    }

    #[tokio::test]
    async fn fetch_can_read_a_later_window_of_large_public_content() {
        let executor = test_executor(
            Arc::new(StaticTransport {
                chunks: vec![b"navigation navigation QUALIFYING PRODUCT 4TB $189.99".to_vec()],
            }),
            Duration::from_secs(1),
            64,
        );

        let result = executor
            .execute(serde_json::json!({
                "url": "https://reader.example/https://retailer.example/search",
                "offset_chars": 22,
                "max_chars": 32
            }))
            .await
            .expect("fetch succeeds");

        assert_eq!(result["content"], "QUALIFYING PRODUCT 4TB $189.99");
        assert_eq!(result["offset_chars"], 22);
    }

    #[tokio::test]
    async fn fetch_times_out_pending_transport() {
        let timeout = Duration::ZERO;
        let executor = test_executor(Arc::new(PendingTransport), timeout, 64);

        let error = executor
            .execute(serde_json::json!({"url": "https://example.com/"}))
            .await
            .expect_err("pending transport must time out");

        assert!(matches!(error, WebFetchError::Timeout(duration) if duration == timeout));
    }
}
