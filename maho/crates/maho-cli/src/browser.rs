// Copyright 2026 The Maho Authors. All rights reserved.

//! Browser connection helpers for CLI commands.
//!
//! Wraps the `maho_browser_mcp::client::BrowserClient` to provide a simpler
//! interface for CLI subcommand handlers: connect, call MCP tools, and handle
//! common error messages.

use std::path::PathBuf;
use std::time::Duration;

use anyhow::{bail, Context, Result};
use maho_browser_mcp::client::BrowserClient;
use maho_browser_mcp::paths::default_socket_path;
use serde_json::Value;

/// Default timeout for MCP tool calls from the CLI.
const CLI_TIMEOUT: Duration = Duration::from_secs(15);

/// Attempt to connect to the running Maho browser instance.
///
/// If `override_path` is provided (from `--socket-path` flag), that path is
/// used instead of the platform default.
///
/// Returns a connected, initialized `BrowserClient` or an error with a
/// user-friendly message.
pub async fn connect(override_path: Option<&str>) -> Result<BrowserClient> {
    let path = match override_path {
        Some(p) => PathBuf::from(p),
        None => default_socket_path(),
    };

    BrowserClient::connect_with_timeout(&path, CLI_TIMEOUT)
        .await
        .map_err(|e| {
            // Provide a helpful message for the most common error case.
            if matches!(e, maho_browser_mcp::error::McpBridgeError::SocketConnect(_)) {
                anyhow::anyhow!(
                    "Browser not running. Start Maho.app or run `maho headless --launch`.\n\
                     (socket: {})\n\
                     (underlying: {})",
                    path.display(),
                    e
                )
            } else {
                anyhow::anyhow!("Failed to connect to browser: {e}")
            }
        })
}

/// Call an MCP tool on the connected browser and return the result payload.
///
/// This wraps the `tools/call` JSON-RPC method, extracting the result content.
pub async fn send_tool_call(client: &BrowserClient, tool: &str, args: Value) -> Result<Value> {
    let params = serde_json::json!({
        "name": tool,
        "arguments": args,
    });

    let result = client
        .call("tools/call", params)
        .await
        .with_context(|| format!("tools/call {tool} failed"))?;

    // MCP tools/call returns { content: [{type: "text", text: "..."}] }
    // We parse the text field as JSON for structured results.
    if let Some(content) = result.get("content") {
        if let Some(arr) = content.as_array() {
            if let Some(first) = arr.first() {
                if let Some(text) = first.get("text").and_then(|t| t.as_str()) {
                    if let Ok(parsed) = serde_json::from_str::<Value>(text) {
                        return Ok(parsed);
                    }
                    // Return as raw string value if not valid JSON
                    return Ok(Value::String(text.to_string()));
                }
            }
        }
    }

    // Fallback: return the raw result
    Ok(result)
}

/// Parse a human duration string like "7d", "24h", "30m", "60s" into seconds.
///
/// Supported suffixes: `d` (days), `h` (hours), `m` (minutes), `s` (seconds).
/// Returns None if the format is invalid.
pub fn parse_duration_to_seconds(s: &str) -> Result<i64> {
    let s = s.trim();
    if s.is_empty() {
        bail!("empty duration string");
    }

    let (num_str, suffix) = s.split_at(s.len() - 1);
    let num: i64 = num_str
        .parse()
        .with_context(|| format!("invalid duration number: {num_str:?}"))?;

    let multiplier = match suffix {
        "d" => 86400,
        "h" => 3600,
        "m" => 60,
        "s" => 1,
        _ => bail!("unknown duration suffix: {suffix:?}. Use d/h/m/s"),
    };

    Ok(num * multiplier)
}

/// Truncate a string to `max` chars, appending "..." if truncated.
pub fn truncate(s: &str, max: usize) -> String {
    if s.chars().count() <= max {
        s.to_string()
    } else {
        let truncated: String = s.chars().take(max.saturating_sub(3)).collect();
        format!("{truncated}...")
    }
}

/// Extract the domain from a URL string.
pub fn extract_domain(url: &str) -> &str {
    // Strip scheme
    let after_scheme = url
        .strip_prefix("https://")
        .or_else(|| url.strip_prefix("http://"))
        .unwrap_or(url);

    // Take up to first /
    after_scheme.split('/').next().unwrap_or(after_scheme)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_duration_days() {
        assert_eq!(parse_duration_to_seconds("7d").unwrap(), 604800);
    }

    #[test]
    fn parse_duration_hours() {
        assert_eq!(parse_duration_to_seconds("24h").unwrap(), 86400);
    }

    #[test]
    fn parse_duration_minutes() {
        assert_eq!(parse_duration_to_seconds("30m").unwrap(), 1800);
    }

    #[test]
    fn parse_duration_seconds() {
        assert_eq!(parse_duration_to_seconds("60s").unwrap(), 60);
    }

    #[test]
    fn parse_duration_invalid() {
        assert!(parse_duration_to_seconds("abc").is_err());
        assert!(parse_duration_to_seconds("").is_err());
        assert!(parse_duration_to_seconds("7x").is_err());
    }

    #[test]
    fn truncate_short() {
        assert_eq!(truncate("hello", 10), "hello");
    }

    #[test]
    fn truncate_long() {
        assert_eq!(truncate("hello world this is long", 10), "hello w...");
    }

    #[test]
    fn extract_domain_https() {
        assert_eq!(extract_domain("https://example.com/path"), "example.com");
    }

    #[test]
    fn extract_domain_http() {
        assert_eq!(extract_domain("http://foo.bar/baz"), "foo.bar");
    }

    #[test]
    fn extract_domain_no_scheme() {
        assert_eq!(extract_domain("example.com/path"), "example.com");
    }
}
