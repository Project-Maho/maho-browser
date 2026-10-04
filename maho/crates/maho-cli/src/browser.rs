// Copyright 2026 The Maho Authors. All rights reserved.

//! Browser connection helpers for CLI commands.
//!
//! Wraps the `maho_browser_mcp::client::BrowserClient` to provide a simpler
//! interface for CLI subcommand handlers.

#[path = "refactor.rs"]
#[doc(hidden)]
pub mod refactor;

use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::Duration;

use anyhow::{bail, Context, Result};
use maho_browser_mcp::client::BrowserClient;
use maho_browser_mcp::error::McpBridgeError;
use maho_browser_mcp::paths::default_socket_path;
use maho_browser_mcp::protocol::ControllerKind;
use serde_json::Value;

/// Default timeout for cheap MCP RPCs from the CLI.
const CLI_DEFAULT_TIMEOUT: Duration = Duration::from_secs(15);
/// Accessibility snapshots and input actions may legitimately wait on renderer
/// work; keep a wider safety window without making cheap verbs sluggish.
const CLI_SLOW_TOOL_TIMEOUT: Duration = Duration::from_secs(60);
/// Zero means no command-line override. `maho browser --timeout <secs>` sets
/// this once at process startup for every browser RPC made by that invocation.
static CLI_TIMEOUT_OVERRIDE_SECS: AtomicU64 = AtomicU64::new(0);

pub fn set_timeout_override(timeout_secs: Option<u64>) -> Result<()> {
    if timeout_secs == Some(0) {
        bail!("--timeout must be greater than 0 seconds");
    }
    CLI_TIMEOUT_OVERRIDE_SECS.store(timeout_secs.unwrap_or(0), Ordering::Relaxed);
    Ok(())
}

fn timeout_override() -> Option<Duration> {
    let seconds = CLI_TIMEOUT_OVERRIDE_SECS.load(Ordering::Relaxed);
    (seconds != 0).then(|| Duration::from_secs(seconds))
}

fn default_tool_timeout(tool: &str) -> Duration {
    if matches!(
        tool,
        "browser_accessibility_snapshot"
            | "page.accessibility_snapshot_v2"
            | "page_accessibility_snapshot_v2"
            | "input.locator_click"
            | "input_locator_click"
            | "browser_locator_click"
            | "input.locator_type"
            | "input_locator_type"
            | "browser_locator_type"
            | "browser_type"
            | "browser_click"
            | "browser_key_press"
            | "browser_select"
            | "browser_screenshot_element"
            | "browser_scroll"
            | "browser_hover"
            | "browser_file_upload_select"
            | "browser_page_content"
            | "browser_page_text"
            | "vault_fill_credential"
            | "vault_fill_totp"
    ) {
        CLI_SLOW_TOOL_TIMEOUT
    } else {
        CLI_DEFAULT_TIMEOUT
    }
}

pub(crate) fn tool_timeout(tool: &str) -> Duration {
    timeout_override().unwrap_or_else(|| default_tool_timeout(tool))
}

fn connection_timeout() -> Duration {
    timeout_override().unwrap_or(CLI_DEFAULT_TIMEOUT)
}

/// Attempt to connect to the running Maho browser instance.
///
/// If `override_path` is provided (from `--socket-path` flag), that path is
/// used instead of the platform default.
///
/// Returns a connected, initialized `BrowserClient` or an error with a
/// user-friendly message.
pub async fn connect(override_path: Option<&str>) -> Result<BrowserClient> {
    connect_as(override_path, ControllerKind::MahoCli).await
}

/// Connect as the persistent interactive CLI controller.
pub async fn connect_repl(override_path: Option<&str>) -> Result<BrowserClient> {
    connect_as(override_path, ControllerKind::MahoCliRepl).await
}

/// Connect as the autonomous CLI agent controller (non-stealing lease admission).
pub async fn connect_autonomous(override_path: Option<&str>) -> Result<BrowserClient> {
    let path = match override_path {
        Some(p) => PathBuf::from(p),
        None => default_socket_path(),
    };

    let result =
        BrowserClient::connect_autonomous_as(&path, connection_timeout(), ControllerKind::MahoCli)
            .await;
    result.map_err(|e| {
        if matches!(e, maho_browser_mcp::error::McpBridgeError::SocketConnect(_)) {
            anyhow::anyhow!(
                "Browser not running. Start Maho.app or run `maho headless --launch`.\n\
                 (socket: {})\n\
                 (underlying: {})",
                path.display(),
                e
            )
        } else {
            anyhow::anyhow!("{e}")
        }
    })
}

/// Call an MCP tool on the connected browser without reconnecting and return
/// the raw result Value directly without CLI human projection.
pub async fn send_raw_tool_call_no_replay(
    client: &BrowserClient,
    tool: &str,
    args: Value,
) -> Result<Value> {
    let params = serde_json::json!({
        "name": tool,
        "arguments": args,
    });
    let timeout = tool_timeout(tool);
    client
        .call_without_reconnect_with_timeout("tools/call", params, timeout)
        .await
        .map_err(|error| describe_tool_call_failure(tool, error))
}

async fn connect_as(
    override_path: Option<&str>,
    controller_kind: ControllerKind,
) -> Result<BrowserClient> {
    let path = match override_path {
        Some(p) => PathBuf::from(p),
        None => default_socket_path(),
    };

    let result = BrowserClient::connect_as(&path, connection_timeout(), controller_kind).await;
    result.map_err(|e| {
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
pub(crate) fn describe_tool_call_failure(tool: &str, error: McpBridgeError) -> anyhow::Error {
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

static V2_DETECTION_CACHE: std::sync::Mutex<Option<std::collections::HashMap<String, bool>>> =
    std::sync::Mutex::new(None);

fn connection_cache_key(
    client: &BrowserClient,
    session_info: Option<&maho_browser_mcp::protocol::SessionInfo>,
) -> String {
    if let Some(session) = session_info {
        if !session.id.is_empty() {
            return format!("{:p}_{}", client, session.id);
        }
    }
    format!("{:p}", client)
}

/// Check if V2 browser-side locator capabilities are available on the connection.
///
/// Feature detection checks if V2 capabilities (`input.locator_click`, `input.locator_type`,
/// or `page.accessibility_snapshot_v2`) are advertised in session info or the live catalog,
/// caching the result per connection to keep semantic command RPCs minimal.
pub async fn is_v2_available(client: &BrowserClient) -> bool {
    let session = client.session_info().await;
    let key = connection_cache_key(client, session.as_ref());

    if let Ok(guard) = V2_DETECTION_CACHE.lock() {
        if let Some(cache) = guard.as_ref() {
            if let Some(&supported) = cache.get(&key) {
                return supported;
            }
        }
    }

    if let Some(ref s) = session {
        if s.id.contains("v2") || s.display_label.to_ascii_lowercase().contains("v2") {
            cache_v2_detection(&key, true);
            return true;
        }
    }

    let is_v2 = match client
        .call_with_timeout("tools/list", serde_json::json!({}), connection_timeout())
        .await
    {
        Ok(discovery) => catalog_advertises_v2(&discovery),
        Err(_) => false,
    };

    cache_v2_detection(&key, is_v2);
    is_v2
}

fn cache_v2_detection(key: &str, supported: bool) {
    if let Ok(mut guard) = V2_DETECTION_CACHE.lock() {
        let cache = guard.get_or_insert_with(std::collections::HashMap::new);
        cache.insert(key.to_string(), supported);
    }
}

pub fn catalog_advertises_v2(discovery: &Value) -> bool {
    if let Some(tools) = discovery.get("tools").and_then(Value::as_array) {
        for tool in tools {
            if let Some(cap_id) = tool.get("capabilityId").and_then(Value::as_str) {
                if cap_id == "input.locator_click"
                    || cap_id == "input.locator_type"
                    || cap_id == "page.accessibility_snapshot_v2"
                {
                    return true;
                }
            }
            if let Some(name) = tool.get("name").and_then(Value::as_str) {
                if name == "input.locator_click"
                    || name == "input.locator_type"
                    || name == "page.accessibility_snapshot_v2"
                    || name == "page_accessibility_snapshot_v2"
                {
                    return true;
                }
            }
        }
    }
    if let Some(surfaces) = discovery
        .get("catalogDiagnostics")
        .and_then(|d| d.get("surfaces"))
        .and_then(Value::as_object)
    {
        for surface in surfaces.values() {
            if let Some(ids) = surface.get("ids").and_then(Value::as_array) {
                for id in ids {
                    if let Some(id_str) = id.as_str() {
                        if id_str == "input.locator_click"
                            || id_str == "input.locator_type"
                            || id_str == "page.accessibility_snapshot_v2"
                        {
                            return true;
                        }
                    }
                }
            }
        }
    }
    false
}

/// Call a control-plane capability on the connected browser via `maho/control/call`.
pub async fn send_control_call(
    client: &BrowserClient,
    capability: &str,
    args: Value,
) -> Result<Value> {
    let params = serde_json::json!({
        "name": capability,
        "arguments": args,
    });
    let result = client
        .call_without_reconnect_with_timeout(
            "maho/control/call",
            params,
            timeout_override().unwrap_or(CLI_DEFAULT_TIMEOUT),
        )
        .await
        .map_err(|error| describe_tool_call_failure(capability, error))?;
    Ok(project_cli_result(result))
}

/// Call an MCP tool on the connected browser and return the result payload.
///
/// This wraps the `tools/call` JSON-RPC method, extracting the result content.
pub async fn send_tool_call(client: &BrowserClient, tool: &str, args: Value) -> Result<Value> {
    if refactor::is_semantic_call(&args) {
        return refactor::execute_semantic_call(client, tool, args).await;
    }
    send_tool_call_with(client, tool, args, true).await
}

/// Call an MCP tool on the connected browser without reconnecting on failure,
/// preventing automatic re-execution of side-effecting operations.
pub async fn send_tool_call_no_replay(
    client: &BrowserClient,
    tool: &str,
    args: Value,
) -> Result<Value> {
    if refactor::is_semantic_call(&args) {
        return refactor::execute_semantic_call(client, tool, args).await;
    }
    send_tool_call_with(client, tool, args, false).await
}

/// Stable failure classes for the Routine MCP boundary.
#[derive(Debug, thiserror::Error)]
pub enum RoutineCallError {
    #[error("{message}")]
    TierLocked { message: String },
    #[error("{0}")]
    Other(#[source] McpBridgeError),
}

/// Run a Routine while preserving the browser's machine-readable failure class.
pub async fn send_routine_run(
    client: &BrowserClient,
    id: &str,
) -> std::result::Result<Value, RoutineCallError> {
    let result = client
        .call_with_timeout(
            "tools/call",
            serde_json::json!({
                "name": "browser_routines_run",
                "arguments": {"id": id},
            }),
            tool_timeout("browser_routines_run"),
        )
        .await
        .map_err(|error| match error {
            McpBridgeError::Rpc {
                message,
                data: Some(ref data),
                ..
            } if data.get("status").and_then(Value::as_str) == Some("routine_tier_locked") => {
                RoutineCallError::TierLocked { message }
            }
            other => RoutineCallError::Other(other),
        })?;
    Ok(project_cli_result(result))
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

    let timeout = tool_timeout(tool);
    let result = if reconnect {
        client
            .call_with_timeout("tools/call", params, timeout)
            .await
    } else {
        client
            .call_without_reconnect_with_timeout("tools/call", params, timeout)
            .await
    }
    .map_err(|error| describe_tool_call_failure(tool, error))?;

    Ok(project_cli_result(result))
}

/// Preserve the browser result while promoting optional receipt metadata into
/// the CLI's machine JSON. Old browsers and hosts without `_meta.mahoReceipt`
/// retain the exact pre-receipt payload shape.
pub fn project_cli_result(result: Value) -> Value {
    let receipt = result
        .get("_meta")
        .and_then(|meta| meta.get("mahoReceipt"))
        .cloned();
    let payload = result
        .get("structuredContent")
        .cloned()
        .or_else(|| {
            result
                .get("content")
                .and_then(Value::as_array)
                .and_then(|content| content.first())
                .and_then(|first| first.get("text"))
                .and_then(Value::as_str)
                .map(|text| {
                    serde_json::from_str::<Value>(text)
                        .unwrap_or_else(|_| Value::String(text.to_string()))
                })
        })
        .unwrap_or(result);

    if let Some(receipt) = receipt {
        return serde_json::json!({
            "result": payload,
            "receipt": receipt,
        });
    }
    if let Ok(execution) =
        serde_json::from_value::<maho_types::tool::BrowserToolExecution>(payload.clone())
    {
        let output = serde_json::from_str(&execution.output_json)
            .unwrap_or_else(|_| Value::String(execution.output_json));
        return serde_json::json!({
            "result": output,
            "receipt": execution.receipt.project(),
        });
    }
    payload
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

    // Split on the final character, never the final byte: a multi-byte suffix
    // must fail as an unknown suffix instead of panicking on a byte boundary.
    let mut chars = s.chars();
    let suffix = match chars.next_back() {
        Some(suffix) => suffix,
        None => bail!("empty duration string"),
    };
    let num_str = chars.as_str();
    let num: i64 = num_str
        .parse()
        .with_context(|| format!("invalid duration number: {num_str:?}"))?;

    let multiplier: i64 = match suffix {
        'd' => 86400,
        'h' => 3600,
        'm' => 60,
        's' => 1,
        _ => bail!("unknown duration suffix: {suffix:?}. Use d/h/m/s"),
    };

    // Checked math: a duration that overflows i64 must be rejected, not panic.
    match num.checked_mul(multiplier) {
        Some(seconds) => Ok(seconds),
        None => bail!("duration out of range: {s:?}"),
    }
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
    fn cli_projection_promotes_receipt_for_json_and_preserves_legacy_results() {
        let receipt = serde_json::json!({
            "schemaVersion": 1,
            "resultVersion": 1,
            "receiptId": "receipt-stable-1",
            "capabilityId": "browser.page.read",
            "controller": {"id": null, "name": "maho-cli", "type": "automation", "plane": "mcp"},
            "target": {"tabId": 7, "origin": "https://example.test"},
            "category": "observe",
            "sensitivity": "low",
            "approval": "not_requested",
            "outcome": {"status": "succeeded", "code": null},
            "timestamps": {"startedAt": 10.0, "completedAt": 11.0}
        });
        let projected = project_cli_result(serde_json::json!({
            "content": [{"type": "text", "text": "{\"ok\":true}"}],
            "structuredContent": {"ok": true},
            "isError": false,
            "_meta": {"mahoReceipt": receipt.clone()}
        }));
        assert_eq!(projected["result"], serde_json::json!({"ok": true}));
        assert_eq!(projected["receipt"], receipt);

        let direct_browser = project_cli_result(serde_json::json!({
            "content": [{"type": "text", "text": serde_json::json!({
                "outputJson": "{\"ok\":true}",
                "receipt": {
                    "capabilityId": "browser.page.read",
                    "executionId": "receipt-stable-1",
                    "metadata": {
                        "controller": {"name": "maho-cli", "type": "automation", "plane": "mcp"},
                        "state": "completed",
                        "password": "must-not-leak"
                    }
                }
            }).to_string()}]
        }));
        assert_eq!(direct_browser["result"], serde_json::json!({"ok": true}));
        assert_eq!(direct_browser["receipt"]["outcome"]["status"], "succeeded");
        assert!(!direct_browser.to_string().contains("must-not-leak"));

        let legacy = project_cli_result(serde_json::json!({
            "content": [{"type": "text", "text": "{\"tabs\":[]}"}]
        }));
        assert_eq!(legacy, serde_json::json!({"tabs": []}));
    }

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
    fn browser_tool_timeouts_keep_expensive_actions_wide_and_cheap_verbs_snappy() {
        for tool in [
            "browser_accessibility_snapshot",
            "browser_type",
            "browser_click",
            "browser_key_press",
            "browser_select",
            "browser_screenshot_element",
            "browser_scroll",
            "browser_hover",
            "browser_page_content",
            "browser_page_text",
            "vault_fill_credential",
            "vault_fill_totp",
        ] {
            assert_eq!(
                default_tool_timeout(tool),
                Duration::from_secs(60),
                "{tool} should use the slow-tool safety window"
            );
        }
        assert_eq!(
            default_tool_timeout("browser_tab_list"),
            Duration::from_secs(15)
        );
        assert_eq!(
            default_tool_timeout("browser_history_search"),
            Duration::from_secs(15)
        );
    }

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
