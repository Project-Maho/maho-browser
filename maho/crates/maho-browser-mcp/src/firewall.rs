// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Rust-side firewall entry point mirroring the C++
//! [`MahoMcpFirewall`](../../../maho-chromium/browser/mcp/maho_mcp_firewall.h)
//! chokepoint.
//!
//! The actual redaction happens on the browser side (C++) before every
//! success response is serialized. The Rust bridge trusts that any
//! response it reads from the browser has already been redacted; this
//! module provides the [`Redacted`] wrapper type that lets tool handlers
//! statically declare "this value came from the browser's firewall".

use crate::redacted::Redacted;
use serde_json::Value;

/// Wrap a `serde_json::Value` that was received from the browser socket
/// (i.e., emerged from `MahoMcpJsonRpc::BuildSuccessResponse` which
/// unconditionally invokes `MahoMcpFirewall::RedactAll`).
///
/// This is the ONLY construction site for a `Redacted<Value>`. Downstream
/// tool handlers that produce credential-sensitive output types (HAR,
/// screenshots, etc.) return `Redacted<T>` and rely on this entry point.
#[must_use]
pub fn trust_browser_response(value: Value) -> Redacted<Value> {
    Redacted::from_firewall(value)
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn trust_browser_response_wraps_value() {
        let v = json!({"pong": true});
        let r = trust_browser_response(v.clone());
        assert_eq!(r.as_inner(), &v);
    }
}
