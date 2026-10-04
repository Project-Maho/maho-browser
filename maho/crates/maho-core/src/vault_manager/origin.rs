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

fn is_label_boundary_child(parent_host: &str, child_host: &str) -> bool {
    child_host.len() > parent_host.len()
        && child_host.ends_with(parent_host)
        && child_host.as_bytes()[child_host.len() - parent_host.len() - 1] == b'.'
}
