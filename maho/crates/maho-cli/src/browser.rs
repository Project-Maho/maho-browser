// Copyright 2026 The Maho Authors. All rights reserved.

//! Browser connection helpers for CLI commands.
//!
//! Wraps the `maho_browser_mcp::client::BrowserClient` to provide a simpler
//! interface for CLI subcommand handlers. The `CliBrowserToolBridge` is the
//! per-run coordinator: it enforces grant → ownership → lease → dispatch
//! ordering for sensitive operations and invalidates the connection after
//! any transport/timeout ambiguity.

use std::path::PathBuf;
use std::time::Duration;

use anyhow::{bail, Context, Result};
use maho_browser_mcp::client::BrowserClient;
use maho_browser_mcp::paths::default_socket_path;
use serde_json::Value;

/// Default timeout for MCP tool calls from the CLI.
const CLI_TIMEOUT: Duration = Duration::from_secs(15);

/// A browser connection for one CLI run. Owns the transport and enforces
/// the capability sandbox lifecycle: grant origins before dispatch, acquire
/// non-stealing leases, and invalidate the connection after ambiguity.
pub struct CliBrowserToolBridge {
    client: BrowserClient,
    /// True after a transport failure or timeout makes the current dispatch
    /// state unknown. Operations are rejected until a fresh connection.
    invalidated: bool,
}

impl CliBrowserToolBridge {
    pub fn new(client: BrowserClient) -> Self {
        Self {
            client,
            invalidated: false,
        }
    }

    /// Refuse to dispatch any operation whose previous outcome is unknown.
    fn check_not_invalidated(&self, operation: &str) -> Result<()> {
        if self.invalidated {
            bail!(
                "browser connection invalidated: the outcome of a previous `{operation}` is unknown — start a new session"
            );
        }
        Ok(())
    }

    /// Mark the connection as invalid after a transport break or timeout that
    /// makes the dispatched operation's outcome indeterminate.
    fn invalidate(&mut self) {
        self.invalidated = true;
    }

    /// Grant an exact origin to the session capability sandbox. Must be called
    /// after the tool permission is approved but before `dispatch`.
    pub async fn grant_exact_origin(&mut self, origin: &str) -> Result<()> {
        self.check_not_invalidated("grant_exact_origin")?;
        let _granted = send_stateful_tool_call(
            &self.client,
            "browser_grant_exact_origin",
            serde_json::json!({ "origin": origin }),
        )
        .await
        .map_err(|e| {
            self.invalidate();
            e
        })?;
        Ok(())
    }

    /// Adopt an ambient user tab into the sandbox. Must be approved by the
    /// permission gate before calling.
    pub async fn adopt_tab(&mut self, tab_id: i64) -> Result<()> {
        self.check_not_invalidated("adopt_tab")?;
        let _adopted = send_stateful_tool_call(
            &self.client,
            "browser_adopt_tab",
            serde_json::json!({ "tab_id": tab_id }),
        )
        .await
        .map_err(|e| {
            self.invalidate();
            e
        })?;
        Ok(())
    }

    /// Dispatch a sensitive browser operation. This is the terminal step
    /// after grant + ownership + lease are all confirmed. Produces a hard
    /// ambiguity error on any transport/timeout outcome.
    pub async fn dispatch(&mut self, tool: &str, args: Value) -> Result<Value> {
        self.check_not_invalidated(tool)?;
        let result = send_stateful_tool_call(&self.client, tool, args.clone()).await;
        match result {
            Err(ref e) if is_transport_ambiguous(e) => {
                self.invalidate();
                Err(anyhow::anyhow!(
                    "transport to browser lost during `{tool}`; operation state unknown — start a new session"
                ))
            }
            other => other,
        }
    }

    /// Convenience: list tabs for discovery/adoption.
    pub async fn tab_list(&mut self) -> Result<Value> {
        self.check_not_invalidated("tab_list")?;
        send_tool_call(&self.client, "browser_tab_list", serde_json::json!({})).await
    }
}

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

/// Renders a failed `tools/call` so the browser's own reason survives.
///
/// The reason matters to callers: the agent recovers from
/// `no eligible active tab` by opening a tab, but only if it can still read
/// that text. Wrapping the error in a generic "tools/call X failed" destroyed
/// the signal and left the agent retrying navigation forever.
fn describe_tool_call_failure(
    tool: &str,
    error: maho_browser_mcp::error::McpBridgeError,
) -> anyhow::Error {
    match &error {
        maho_browser_mcp::error::McpBridgeError::Io(io_err) if is_io_transport_loss(io_err) => {
            anyhow::anyhow!(
                "transport to browser lost during `{tool}`; operation state unknown — start a new session"
            )
        }
        maho_browser_mcp::error::McpBridgeError::Rpc {
            code,
            message: _message,
            ..
        } if *code == -32005 => {
            anyhow::anyhow!("no eligible target tab for `{tool}`")
        }
        other => anyhow::anyhow!("tools/call {tool} failed: {other}"),
    }
}

/// True for Io errors whose timing makes the dispatched operation's outcome
/// indeterminate — broken pipe, connection reset, and timed-out writes.
fn is_io_transport_loss(e: &std::io::Error) -> bool {
    use std::io::ErrorKind;
    matches!(
        e.kind(),
        ErrorKind::BrokenPipe
            | ErrorKind::ConnectionReset
            | ErrorKind::ConnectionAborted
            | ErrorKind::UnexpectedEof
            | ErrorKind::TimedOut
    )
}

/// True when the error signals a transport break or timeout. Used by
/// `CliBrowserToolBridge::dispatch` to invalidate after ambiguity.
pub fn is_transport_ambiguous(e: &anyhow::Error) -> bool {
    format!("{e}").contains("operation state unknown")
}

/// Call an MCP tool on the connected browser and return the result payload.
///
/// This wraps the `tools/call` JSON-RPC method, extracting the result content.
pub async fn send_tool_call(client: &BrowserClient, tool: &str, args: Value) -> Result<Value> {
    send_tool_call_with(client, tool, args, true).await
}

/// Send one stateful agent tool call without transparently reconnecting.
/// A new connection loses the browser-side allowlist and lease, and replaying a
/// mutation after an ambiguous transport failure can duplicate its effect.
pub async fn send_stateful_tool_call(
    client: &BrowserClient,
    tool: &str,
    args: Value,
) -> Result<Value> {
    send_tool_call_with(client, tool, args, false).await
}

async fn send_tool_call_with(
    client: &BrowserClient,
    tool: &str,
    args: Value,
    reconnect: bool,
) -> Result<Value> {
    let params = serde_json::json!({
        "name": tool,
        "arguments": args,
    });

    let result = if reconnect {
        client.call("tools/call", params).await
    } else {
        client.call_without_reconnect("tools/call", params).await
    }
    .map_err(|error| describe_tool_call_failure(tool, error))?;

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
    #[test]
    fn tool_call_failure_preserves_browser_reason() {
        let error = describe_tool_call_failure(
            "browser_navigate",
            maho_browser_mcp::error::McpBridgeError::Rpc {
                code: -32005,
                message: "no eligible active tab".to_string(),
                data: None,
            },
        );

        let rendered = format!("{error:#}");
        assert!(
            rendered.contains("no eligible target tab"),
            "the structured reason must be preserved, got: {rendered}"
        );
        assert!(
            rendered.contains("browser_navigate"),
            "the tool name must remain, got: {rendered}"
        );
    }

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
