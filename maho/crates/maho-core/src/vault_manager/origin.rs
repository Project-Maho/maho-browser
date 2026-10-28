//! Trusted credential-origin canonicalization and exact/subdomain matching.
//!
//! The core trust boundary: every credential origin is normalized here before it
//! is encrypted or used for duplicate/match decisions. We parse with the
//! already-present `reqwest::Url` (IDNA-to-ASCII, host lowercasing, default-port
//! removal come for free) and reject — never silently strip — userinfo, query,
//! fragment, and non-root paths. Subdomain matching is label-boundary-safe and
//! confined to the same PSL registrable domain.

use maho_types::vault::CredentialOrigin;

use super::item_error::VaultCrudError;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum OriginMatchPolicy {
    Exact,
    Subdomain,
}

pub(crate) fn canonicalize_origin(raw: &str) -> Result<CredentialOrigin, VaultCrudError> {
    let invalid = || VaultCrudError::InvalidOrigin {
        value: raw.to_string(),
    };
    let url = reqwest::Url::parse(raw).map_err(|_| invalid())?;
    let scheme = url.scheme();
    if scheme != "http" && scheme != "https" {
        return Err(invalid());
    }
    if !url.username().is_empty() || url.password().is_some() {
        return Err(invalid());
    }
    if url.query().is_some() || url.fragment().is_some() {
        return Err(invalid());
    }
    if url.path() != "/" {
        return Err(invalid());
    }
    let host = url
        .host_str()
        .filter(|host| !host.is_empty())
        .ok_or_else(invalid)?;
    let canonical = match url.port() {
        Some(port) => format!("{scheme}://{host}:{port}"),
        None => format!("{scheme}://{host}"),
    };
    CredentialOrigin::try_from(canonical).map_err(|_| invalid())
}

pub(crate) fn origin_matches(
    stored: &CredentialOrigin,
    requested: &CredentialOrigin,
    policy: OriginMatchPolicy,
) -> bool {
    match policy {
        OriginMatchPolicy::Exact => stored == requested,
        OriginMatchPolicy::Subdomain => subdomain_matches(stored, requested),
    }
}

fn subdomain_matches(stored: &CredentialOrigin, requested: &CredentialOrigin) -> bool {
    let (Ok(stored_url), Ok(requested_url)) = (
        reqwest::Url::parse(stored.as_str()),
        reqwest::Url::parse(requested.as_str()),
    ) else {
        return false;
    };
    if stored_url.scheme() != requested_url.scheme() {
        return false;
    }
    if stored_url.port_or_known_default() != requested_url.port_or_known_default() {
        return false;
    }
    let (Some(stored_host), Some(requested_host)) =
        (stored_url.host_str(), requested_url.host_str())
    else {
        return false;
    };
    if stored_host == requested_host {
        return true;
    }
    if !is_label_boundary_child(stored_host, requested_host) {
        return false;
    }
    match (
        psl::domain_str(stored_host),
        psl::domain_str(requested_host),
    ) {
        (Some(stored_domain), Some(requested_domain)) => stored_domain == requested_domain,
        _ => false,
    }
}

/// Public, string-level entry point for credential origin binding: returns
/// true only when both origins canonicalize and `requested` matches `stored`
/// under `policy`. Any canonicalization failure (opaque/non-http origin,
/// userinfo, path, query) fails closed to `false`.
pub fn credential_origin_matches(stored: &str, requested: &str, policy: OriginMatchPolicy) -> bool {
    match (canonicalize_origin(stored), canonicalize_origin(requested)) {
        (Ok(stored), Ok(requested)) => origin_matches(&stored, &requested, policy),
        _ => false,
    }
}

/// Chromium-style public-suffix (PSL) affiliation: both origins are https,
/// share a port, differ in host, and share a registrable domain (eTLD+1)
/// under a known public suffix. IP literals, localhost, and hosts without a
/// determinable registrable domain never match.
pub(crate) fn public_suffix_matches(
    stored: &CredentialOrigin,
    requested: &CredentialOrigin,
) -> bool {
    let (Ok(stored_url), Ok(requested_url)) = (
        reqwest::Url::parse(stored.as_str()),
        reqwest::Url::parse(requested.as_str()),
    ) else {
        return false;
    };
    if stored_url.scheme() != "https" || requested_url.scheme() != "https" {
        return false;
    }
    if stored_url.port_or_known_default() != requested_url.port_or_known_default() {
        return false;
    }
    // `domain()` is `None` for IPv4/IPv6 literals.
    let (Some(stored_host), Some(requested_host)) = (stored_url.domain(), requested_url.domain())
    else {
        return false;
    };
    if stored_host == requested_host || is_localhost(stored_host) || is_localhost(requested_host) {
        return false;
    }
    match (
        known_registrable_domain(stored_host),
        known_registrable_domain(requested_host),
    ) {
        (Some(stored_domain), Some(requested_domain)) => stored_domain == requested_domain,
        _ => false,
    }
}

fn is_localhost(host: &str) -> bool {
    host == "localhost" || host.ends_with(".localhost")
}

fn known_registrable_domain(host: &str) -> Option<&[u8]> {
    let domain = psl::domain(host.as_bytes())?;
    domain.suffix().is_known().then(|| domain.as_bytes())
}

fn is_label_boundary_child(parent_host: &str, child_host: &str) -> bool {
    child_host.len() > parent_host.len()
        && child_host.ends_with(parent_host)
        && child_host.as_bytes()[child_host.len() - parent_host.len() - 1] == b'.'
}
