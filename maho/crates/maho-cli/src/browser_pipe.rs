// Copyright 2026 The Maho Authors. All rights reserved.

//! Machine-readable persistent browser automation pipe.
//!
//! Provides an NDJSON streaming interface over stdin/stdout holding a single
//! persistent browser connection. Maintains session state (such as accessibility
//! snapshot token chaining) across consecutive operations.

use std::time::Duration;

use anyhow::Result;
use maho_browser_mcp::client::BrowserClient;
use maho_browser_mcp::error::McpBridgeError;
use serde::{Deserialize, Serialize};
use serde_json::{json, Map, Value};
use tokio::io::{AsyncBufRead, AsyncBufReadExt, AsyncWrite, AsyncWriteExt};

/// Session state maintained across pipe operations.
#[derive(Debug, Default, Clone)]
pub struct PipeSession {
    /// The most recent accessibility snapshot token, chained into differential snapshots.
    pub last_snapshot_token: Option<String>,
}

/// Structured error payload in NDJSON responses.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct PipeErrorDetail {
    pub code: String,
    pub message: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub hint: Option<String>,
}

/// NDJSON response framing.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct PipeResponse {
    pub id: Value,
    pub ok: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub result: Option<Value>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<PipeErrorDetail>,
}

impl PipeResponse {
    pub fn success(id: Value, result: Value) -> Self {
        Self {
            id,
            ok: true,
            result: Some(result),
            error: None,
        }
    }

    pub fn error(
        id: Value,
        code: impl Into<String>,
        message: impl Into<String>,
        hint: Option<String>,
    ) -> Self {
        Self {
            id,
            ok: false,
            result: None,
            error: Some(PipeErrorDetail {
                code: code.into(),
                message: message.into(),
                hint,
            }),
        }
    }

    pub fn bad_request(message: impl Into<String>) -> Self {
        Self::error(Value::Null, "bad_request", message, None)
    }
}

/// Extracts inner payload if wrapped in MCP content or structuredContent envelope.
pub fn unwrap_mcp_result(result: Value) -> Value {
    if let Some(structured) = result.get("structuredContent") {
        return structured.clone();
    }
    if let Some(content) = result.get("content").and_then(Value::as_array) {
        if let Some(first) = content.first() {
            if let Some(text) = first.get("text").and_then(Value::as_str) {
                if let Ok(parsed) = serde_json::from_str::<Value>(text) {
                    return parsed;
                } else {
                    return Value::String(text.to_string());
                }
            }
        }
    }
    result
}

fn map_bridge_error(err: &McpBridgeError) -> (String, String, Option<String>) {
    match err {
        McpBridgeError::Rpc {
            code,
            message,
            data,
        } => {
            let code_str = match code {
                -32601 => "method_not_found".to_string(),
                -32602 => "invalid_params".to_string(),
                -32005 => "no_target_tab".to_string(),
                _ => "rpc_error".to_string(),
            };
            let hint = data.as_ref().and_then(|d| {
                d.get("hint")
                    .and_then(Value::as_str)
                    .map(ToString::to_string)
            });
            (code_str, message.clone(), hint)
        }
        McpBridgeError::Timeout(d) => (
            "timeout".to_string(),
            format!("Operation timed out after {d:?}"),
            None,
        ),
        McpBridgeError::Io(io_err) => (
            "transport_error".to_string(),
            format!("I/O error: {io_err}"),
            None,
        ),
        other => ("browser_error".to_string(), format!("{other}"), None),
    }
}

/// Check if a URL matches a target string or wildcard pattern.
pub fn matches_url_pattern(actual: &str, pattern: &str) -> bool {
    if pattern == "*" {
        return true;
    }
    if pattern.contains('*') {
        let parts: Vec<&str> = pattern.split('*').collect();
        let mut cur = actual;
        for (i, part) in parts.iter().enumerate() {
            if part.is_empty() {
                continue;
            }
            if i == 0 && !actual.starts_with(part) {
                return false;
            }
            if i == parts.len() - 1 && !actual.ends_with(part) {
                return false;
            }
            if let Some(pos) = cur.find(part) {
                cur = &cur[pos + part.len()..];
            } else {
                return false;
            }
        }
        true
    } else {
        actual.contains(pattern)
    }
}

/// Update session snapshot token from an observe or action result.
fn update_snapshot_token(session: &mut PipeSession, result: &Value) {
    if let Some(tok) = result
        .get("observation")
        .and_then(|o| o.get("snapshot_token"))
        .and_then(Value::as_str)
    {
        session.last_snapshot_token = Some(tok.to_string());
    } else if let Some(tok) = result.get("snapshot_token").and_then(Value::as_str) {
        session.last_snapshot_token = Some(tok.to_string());
    }
}

async fn handle_observe(
    session: &mut PipeSession,
    client: &BrowserClient,
    id: Value,
    req: &Map<String, Value>,
) -> PipeResponse {
    let mut args = Map::new();
    if let Some(tab_id) = req.get("tab_id").and_then(Value::as_i64) {
        args.insert("tab_id".to_string(), Value::Number(tab_id.into()));
    }
    if let Some(mode) = req.get("mode").and_then(Value::as_str) {
        args.insert("mode".to_string(), Value::String(mode.to_string()));
    }
    if let Some(scope) = req.get("scope") {
        if let Some(sel) = scope.as_str() {
            args.insert("scope".to_string(), json!({ "selector": sel }));
        } else if scope.is_object() {
            args.insert("scope".to_string(), scope.clone());
        }
    }
    let since = req
        .get("since_snapshot_token")
        .and_then(Value::as_str)
        .map(str::to_string)
        .or_else(|| session.last_snapshot_token.clone());
    if let Some(s) = since {
        args.insert("since_snapshot_token".to_string(), Value::String(s));
    }
    if let Some(mb) = req.get("max_bytes").and_then(Value::as_u64) {
        args.insert("max_bytes".to_string(), Value::Number(mb.into()));
    }
    if let Some(md) = req.get("max_depth").and_then(Value::as_u64) {
        args.insert("max_depth".to_string(), Value::Number(md.into()));
    }
    if let Some(ih) = req.get("include_hidden").and_then(Value::as_bool) {
        args.insert("include_hidden".to_string(), Value::Bool(ih));
    }

    match client
        .call_capability("page.accessibility_snapshot_v2", Value::Object(args))
        .await
    {
        Ok(raw) => {
            let res = unwrap_mcp_result(raw);
            update_snapshot_token(session, &res);
            PipeResponse::success(id, res)
        }
        Err(err) => {
            let (code, message, hint) = map_bridge_error(&err);
            PipeResponse::error(id, code, message, hint)
        }
    }
}


async fn handle_click(
    session: &mut PipeSession,
    client: &BrowserClient,
    id: Value,
    req: &Map<String, Value>,
) -> PipeResponse {
    let locator = match req.get("locator") {
        Some(loc) if !loc.is_null() => loc,
        _ => {
            return PipeResponse::error(
                id,
                "invalid_params",
                "Missing required 'locator' field for click",
                None,
            )
        }
    };

    let mut args = Map::new();
    args.insert("locator".to_string(), locator.clone());
    let observe_mode = req
        .get("observe")
        .and_then(Value::as_str)
        .unwrap_or("diff");
    args.insert("observe".to_string(), Value::String(observe_mode.to_string()));
    args.insert("lease".to_string(), Value::String("scoped".to_string()));
    if let Some(wait) = req.get("wait") {
        args.insert("wait".to_string(), wait.clone());
    }
    if let Some(tab_id) = req.get("tab_id").and_then(Value::as_i64) {
        args.insert("tab_id".to_string(), Value::Number(tab_id.into()));
    }

    match client
        .call_capability("input.locator_click", Value::Object(args))
        .await
    {
        Ok(raw) => {
            let res = unwrap_mcp_result(raw);
            update_snapshot_token(session, &res);
            PipeResponse::success(id, res)
        }
        Err(err) => {
            let (code, message, hint) = map_bridge_error(&err);
            PipeResponse::error(id, code, message, hint)
        }
    }
}

async fn handle_type(
    session: &mut PipeSession,
    client: &BrowserClient,
    id: Value,
    req: &Map<String, Value>,
) -> PipeResponse {
    let locator = match req.get("locator") {
        Some(loc) if !loc.is_null() => loc,
        _ => {
            return PipeResponse::error(
                id,
                "invalid_params",
                "Missing required 'locator' field for type",
                None,
            )
        }
    };

    let text = match req.get("text").and_then(Value::as_str) {
        Some(t) => t,
        None => {
            return PipeResponse::error(
                id,
                "invalid_params",
                "Missing required 'text' field for type",
                None,
            )
        }
    };

    let mut args = Map::new();
    args.insert("locator".to_string(), locator.clone());
    args.insert("text".to_string(), Value::String(text.to_string()));
    if let Some(submit) = req.get("submit").and_then(Value::as_bool) {
        args.insert("submit".to_string(), Value::Bool(submit));
    }
    let observe_mode = req
        .get("observe")
        .and_then(Value::as_str)
        .unwrap_or("diff");
    args.insert("observe".to_string(), Value::String(observe_mode.to_string()));
    args.insert("lease".to_string(), Value::String("scoped".to_string()));
    if let Some(wait) = req.get("wait") {
        args.insert("wait".to_string(), wait.clone());
    }
    if let Some(tab_id) = req.get("tab_id").and_then(Value::as_i64) {
        args.insert("tab_id".to_string(), Value::Number(tab_id.into()));
    }

    match client
        .call_capability("input.locator_type", Value::Object(args))
        .await
    {
        Ok(raw) => {
            let res = unwrap_mcp_result(raw);
            update_snapshot_token(session, &res);
            PipeResponse::success(id, res)
        }
        Err(err) => {
            let (code, message, hint) = map_bridge_error(&err);
            PipeResponse::error(id, code, message, hint)
        }
    }
}

/// Handle a `grant` op: grant one exact origin to this pipe session's
/// capability sandbox (browser_grant_exact_origin). The grant lives on this
/// session's connection, so re-issue it in the same session before
/// dispatching actions on that origin.
async fn handle_grant(
    _session: &mut PipeSession,
    client: &BrowserClient,
    id: Value,
    req: &Map<String, Value>,
) -> PipeResponse {
    let origin = match req.get("origin").and_then(Value::as_str) {
        Some(o) if !o.trim().is_empty() => o.trim().to_string(),
        _ => {
            return PipeResponse::error(
                id,
                "invalid_params",
                "Missing required 'origin' field for grant",
                None,
            )
        }
    };

    match client
        .call_capability("browser_grant_exact_origin", json!({ "origin": origin }))
        .await
    {
        Ok(raw) => {
            let _granted = unwrap_mcp_result(raw);
            PipeResponse::success(id, json!({ "granted": true, "origin": origin }))
        }
        Err(err) => {
            let (code, message, hint) = map_bridge_error(&err);
            PipeResponse::error(id, code, message, hint)
        }
    }
}

async fn handle_navigate(
    _session: &mut PipeSession,
    client: &BrowserClient,
    id: Value,
    req: &Map<String, Value>,
) -> PipeResponse {
    let url = match req.get("url").and_then(Value::as_str) {
        Some(u) => u,
        None => {
            return PipeResponse::error(
                id,
                "invalid_params",
                "Missing required 'url' field for navigate",
                None,
            )
        }
    };

    let mut args = Map::new();
    args.insert("url".to_string(), Value::String(url.to_string()));
    if let Some(tab_id) = req.get("tab_id").and_then(Value::as_i64) {
        args.insert("tab_id".to_string(), Value::Number(tab_id.into()));
    }

    let nav_result = match client
        .call_capability("navigation.navigate", Value::Object(args))
        .await
    {
        Ok(raw) => unwrap_mcp_result(raw),
        Err(err) => {
            let (code, message, hint) = map_bridge_error(&err);
            return PipeResponse::error(id, code, message, hint);
        }
    };

    // If wait was specified alongside navigate
    if let Some(wait_val) = req.get("wait") {
        let should_wait_nav = match wait_val {
            Value::Bool(b) => *b,
            Value::String(s) => s == "navigation" || s == "auto",
            Value::Object(map) => {
                map.get("mode")
                    .and_then(Value::as_str)
                    .map(|m| m == "navigation" || m == "auto")
                    .unwrap_or(true)
            }
            _ => false,
        };

        if should_wait_nav {
            let timeout_ms = match wait_val {
                Value::Object(map) => map.get("timeout_ms").and_then(Value::as_u64).unwrap_or(30000),
                _ => 30000,
            };
            let mut wait_args = Map::new();
            wait_args.insert(
                "timeout_ms".to_string(),
                Value::Number(timeout_ms.into()),
            );
            if let Some(tab_id) = req.get("tab_id").and_then(Value::as_i64) {
                wait_args.insert("tab_id".to_string(), Value::Number(tab_id.into()));
            }
            let _ = client
                .call_capability("navigation.wait", Value::Object(wait_args))
                .await;
        } else if let Value::Object(map) = wait_val {
            if let Some(sel) = map.get("selector").and_then(Value::as_str) {
                let timeout_ms = map.get("timeout_ms").and_then(Value::as_u64).unwrap_or(10000);
                let mut sel_args = Map::new();
                sel_args.insert("selector".to_string(), Value::String(sel.to_string()));
                sel_args.insert("timeout_ms".to_string(), Value::Number(timeout_ms.into()));
                if let Some(tab_id) = req.get("tab_id").and_then(Value::as_i64) {
                    sel_args.insert("tab_id".to_string(), Value::Number(tab_id.into()));
                }
                let _ = client
                    .call_capability("page.wait_for_selector", Value::Object(sel_args))
                    .await;
            }
        }
    }

    PipeResponse::success(id, nav_result)
}

async fn handle_wait(
    _session: &mut PipeSession,
    client: &BrowserClient,
    id: Value,
    req: &Map<String, Value>,
) -> PipeResponse {
    let selector = req.get("selector").and_then(Value::as_str);
    let url_pattern = req
        .get("url")
        .or_else(|| req.get("pattern"))
        .or_else(|| req.get("url_pattern"))
        .and_then(Value::as_str);
    let timeout_ms = req.get("timeout_ms").and_then(Value::as_u64).unwrap_or(10000);

    if let Some(sel) = selector {
        let mut args = Map::new();
        args.insert("selector".to_string(), Value::String(sel.to_string()));
        args.insert("timeout_ms".to_string(), Value::Number(timeout_ms.into()));
        if let Some(tab_id) = req.get("tab_id").and_then(Value::as_i64) {
            args.insert("tab_id".to_string(), Value::Number(tab_id.into()));
        }

        match client
            .call_capability("page.wait_for_selector", Value::Object(args))
            .await
        {
            Ok(raw) => {
                let res = unwrap_mcp_result(raw);
                PipeResponse::success(id, res)
            }
            Err(err) => {
                let (code, message, hint) = map_bridge_error(&err);
                PipeResponse::error(id, code, message, hint)
            }
        }
    } else if let Some(pattern) = url_pattern {
        let start = std::time::Instant::now();
        let timeout = Duration::from_millis(timeout_ms);
        let mut matched_url: Option<String> = None;

        while start.elapsed() < timeout {
            let tab_list_res = client
                .call_capability("tab.list", json!({}))
                .await;
            if let Ok(raw) = tab_list_res {
                let res = unwrap_mcp_result(raw);
                if let Some(tabs) = res.get("tabs").and_then(Value::as_array) {
                    let target_tab = if let Some(tab_id) = req.get("tab_id").and_then(Value::as_i64) {
                        tabs.iter().find(|t| t.get("id") == Some(&Value::Number(tab_id.into())))
                    } else {
                        tabs.iter().find(|t| t.get("is_active") == Some(&Value::Bool(true)))
                    };
                    if let Some(tab) = target_tab {
                        if let Some(url) = tab.get("url").and_then(Value::as_str) {
                            if matches_url_pattern(url, pattern) {
                                matched_url = Some(url.to_string());
                                break;
                            }
                        }
                    }
                }
            }
            tokio::time::sleep(Duration::from_millis(100)).await;
        }

        if let Some(url) = matched_url {
            PipeResponse::success(id, json!({ "found": true, "url": url }))
        } else {
            PipeResponse::error(
                id,
                "timeout",
                format!("Timed out waiting for URL pattern '{pattern}' after {timeout_ms}ms"),
                None,
            )
        }
    } else {
        PipeResponse::error(
            id,
            "invalid_params",
            "Either 'selector' or 'url' is required for wait",
            None,
        )
    }
}

/// Process a single NDJSON line.
pub async fn handle_pipe_line(
    session: &mut PipeSession,
    client: &BrowserClient,
    line: &str,
) -> PipeResponse {
    let parsed: Value = match serde_json::from_str(line) {
        Ok(v) => v,
        Err(e) => return PipeResponse::bad_request(format!("Invalid JSON: {e}")),
    };

    let obj = match parsed {
        Value::Object(map) => map,
        _ => return PipeResponse::bad_request("Request must be a JSON object"),
    };

    let id = obj.get("id").cloned().unwrap_or(Value::Null);

    let op = match obj.get("op").and_then(Value::as_str) {
        Some(op) => op,
        None => {
            return PipeResponse::error(
                id,
                "invalid_params",
                "Missing 'op' field",
                None,
            );
        }
    };

    match op {
        "grant" => handle_grant(session, client, id, &obj).await,
        "observe" => handle_observe(session, client, id, &obj).await,
        "click" => handle_click(session, client, id, &obj).await,
        "type" => handle_type(session, client, id, &obj).await,
        "navigate" => handle_navigate(session, client, id, &obj).await,
        "wait" => handle_wait(session, client, id, &obj).await,
        other => PipeResponse::error(
            id,
            "unknown_op",
            format!("Unknown operation: {other}"),
            None,
        ),
    }
}

/// Persistent pipe read-eval-print loop over NDJSON streams.
pub async fn run_pipe_loop<R, W>(
    reader: R,
    mut writer: W,
    client: &BrowserClient,
) -> Result<()>
where
    R: AsyncBufRead + Unpin,
    W: AsyncWrite + Unpin,
{
    let mut session = PipeSession::default();
    let mut lines = reader.lines();

    while let Some(line) = lines.next_line().await? {
        let trimmed = line.trim();
        if trimmed.is_empty() {
            continue;
        }
        let resp = handle_pipe_line(&mut session, client, trimmed).await;
        let mut out = serde_json::to_vec(&resp)?;
        out.push(b'\n');
        writer.write_all(&out).await?;
        writer.flush().await?;
    }

    Ok(())
}

/// Connect to browser and start the machine-readable persistent pipe.
pub async fn run_pipe(socket_path: Option<&str>) -> Result<()> {
    let client = crate::browser::connect(socket_path).await?;
    let stdin = tokio::io::BufReader::new(tokio::io::stdin());
    let stdout = tokio::io::stdout();
    run_pipe_loop(stdin, stdout, &client).await
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_matches_url_pattern() {
        assert!(matches_url_pattern("https://example.com/login", "example.com"));
        assert!(matches_url_pattern("https://example.com/login", "*login"));
        assert!(matches_url_pattern("https://example.com/login", "https://*.com/*"));
        assert!(matches_url_pattern("https://example.com/login", "*"));
        assert!(!matches_url_pattern("https://example.com/dashboard", "*login"));
    }
}
