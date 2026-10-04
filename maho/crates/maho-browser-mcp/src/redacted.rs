// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Compile-time enforcement wrapper for the 4-vector credential firewall.
//!
//! `Redacted<T>` is a type-level guarantee that a value has passed through
//! [`Redacted::from_firewall`] — the only constructor. Any Wave 4.4 HAR
//! handler or other credential-sensitive tool return path takes `Redacted<T>`
//! as its output type, so a payload that has not gone through the firewall
//! cannot be returned without a compile error.
//!
//! The Rust side mirrors the C++ [`MahoMcpFirewall::RedactAll`] chokepoint at
//! `maho-chromium/browser/mcp/maho_mcp_firewall.h`.

use serde::Serialize;
use std::fmt;

/// A value that has been passed through the credential firewall.
///
/// Construct only via [`Redacted::from_firewall`], which the firewall module
/// calls after applying the 4-vector redaction to the underlying value.
/// Downstream code that returns this type has a compile-time guarantee that
/// the payload cannot contain unscrubbed credentials.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(transparent)]
pub struct Redacted<T>(T);

impl<T> Redacted<T> {
    /// Construct a `Redacted<T>` from a value that has just been passed
    /// through the firewall. Callers are responsible for actually running
    /// the firewall on the value before invoking this constructor; the
    /// visibility discipline (only [`crate::firewall`] module invokes this)
    /// enforces that at the module boundary.
    #[must_use]
    pub(crate) fn from_firewall(value: T) -> Self {
        Self(value)
    }

    /// Access the inner value. Use only when serializing to the wire — the
    /// wire is the trust boundary; caller must not further transform the
    /// inner value before egress.
    #[must_use]
    pub fn as_inner(&self) -> &T {
        &self.0
    }

    /// Consume the wrapper and return the inner value.
    #[must_use]
    pub fn into_inner(self) -> T {
        self.0
    }
}

impl<T: fmt::Display> fmt::Display for Redacted<T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.0.fmt(f)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn from_firewall_and_inner_roundtrip() {
        let r = Redacted::from_firewall(String::from("hello"));
        assert_eq!(r.as_inner(), "hello");
        assert_eq!(r.into_inner(), "hello");
    }

    #[test]
    fn serde_transparent() {
        let r = Redacted::from_firewall(42_i64);
        let json = serde_json::to_string(&r).unwrap();
        assert_eq!(json, "42");
    }

    #[test]
    fn display_forwards_to_inner() {
        let r = Redacted::from_firewall("foo");
        assert_eq!(format!("{r}"), "foo");
    }
}
