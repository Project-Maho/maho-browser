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

/// Ops a pipe line may set in `op`. Included in every protocol error so a
/// truncated reader still sees the next legal call.
pub const PIPE_OPS: &str = "act, native, grant, observe, click, type, navigate, wait, upload";

/// One legal line. Quoted so it can be copied into stdin as-is.
pub const PIPE_EXAMPLE: &str = r#"{"id":1,"op":"observe","tab_id":1,"mode":"interactive"}"#;

pub fn pipe_protocol_hint() -> String {
    format!("Valid ops: {PIPE_OPS}. Example: {PIPE_EXAMPLE}")
}

fn missing_op_message() -> String {
    format!("Missing 'op' field. {}", pipe_protocol_hint())
}

fn unknown_op_message(op: &str) -> String {
    format!("Unknown operation: {op}. {}", pipe_protocol_hint())
}

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
    #[serde(skip_serializing_if = "Option::is_none")]
    pub rpc_code: Option<i64>,
    /// Original structured RPC data; this mapping does not log request arguments.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub rpc_data: Option<Value>,
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
                rpc_code: None,
                rpc_data: None,
            }),
        }
    }

    fn bridge_error(id: Value, err: &McpBridgeError) -> Self {
        let (code, message, hint) = map_bridge_error(err);
        let (rpc_code, rpc_data) = match err {
            McpBridgeError::Rpc { code, data, .. } => (Some(i64::from(*code)), data.clone()),
            _ => (None, None),
        };
        Self {
            id,
            ok: false,
            result: None,
            error: Some(PipeErrorDetail {
                code,
                message,
                hint,
                rpc_code,
                rpc_data,
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

/// Forward only the existing act-and-observe contract, without implicit authority.
async fn handle_act(
    session: &mut PipeSession,
    client: &BrowserClient,
    id: Value,
    req: &Map<String, Value>,
) -> PipeResponse {
    // Follow the executing session handler, not the advertised string schema:
    // anything other than object kind=type would otherwise become a click.
    let action = req.get("action").and_then(Value::as_object);
    if !matches!(
        action.and_then(|a| a.get("kind")).and_then(Value::as_str),
        Some("click" | "type")
    ) {
        return PipeResponse::error(
            id,
            "invalid_params",
            "'action' must be an object with kind click or type; use separate observe/navigate ops",
            None,
        );
    }
    let locator = req
        .get("locator")
        .or_else(|| action.and_then(|a| a.get("locator")))
        .and_then(Value::as_object);
    let valid_locator = locator.is_some_and(|loc| {
        let valid_ref = loc
            .get("ref")
            .is_none_or(|value| value.as_i64().is_some_and(|r| i32::try_from(r).is_ok()));
        valid_ref
            && (loc.contains_key("ref")
                || loc.get("css").and_then(Value::as_str).is_some()
                || (loc.get("role").and_then(Value::as_str).is_some()
                    && loc.get("name").and_then(Value::as_str).is_some()))
    });
    if !valid_locator || req.get("wait").is_some_and(|wait| !wait.is_object()) {
        return PipeResponse::error(
            id,
            "invalid_params",
            "act requires a locator with an integer ref, css, or role/name; wait must be an object",
            None,
        );
    }
    let mut args = Map::new();
    for key in [
        "action",
        "locator",
        "text",
        "wait",
        "observe",
        "since_snapshot_token",
        "tab_id",
        "lease",
    ] {
        if let Some(value) = req.get(key) {
            args.insert(key.to_string(), value.clone());
        }
    }
    // Tab-binding contract: an act without an explicit tab_id would otherwise
    // fall back to the focused tab on the server. Reject here with an example
    // so callers fix their script instead of racing another controller.
    let bound_tab_id = args
        .get("tab_id")
        .and_then(Value::as_i64)
        .filter(|tab| *tab != 0);
    if bound_tab_id.is_none() {
        return PipeResponse::error(
            id,
            "invalid_params",
            "Missing required 'tab_id' field for act: pipe never guesses the focused tab. Example: {\"id\":1,\"op\":\"act\",\"tab_id\":1,\"action\":{\"kind\":\"click\"},\"locator\":{\"css\":\"button\"}}",
            None,
        );
    }
    match client
        .call_capability_with_timeout(
            "browser.act_and_observe",
            Value::Object(args),
            crate::browser::tool_timeout("browser.act_and_observe"),
        )
        .await
    {
        Ok(raw) => {
            let res = unwrap_mcp_result(raw);
            update_snapshot_token(session, &res);
            PipeResponse::success(id, res)
        }
        Err(err) => PipeResponse::bridge_error(id, &err),
    }
}

fn parse_point_f64(v: Option<&Value>) -> Option<(f64, f64)> {
    match v? {
        Value::Array(arr) if arr.len() >= 2 => {
            let x = arr[0].as_f64()?;
            let y = arr[1].as_f64()?;
            Some((x, y))
        }
        Value::Object(obj) => {
            let x = obj.get("x").and_then(Value::as_f64)?;
            let y = obj.get("y").and_then(Value::as_f64)?;
            Some((x, y))
        }
        _ => None,
    }
}

fn parse_rect_f64(v: Option<&Value>) -> Option<(f64, f64, f64, f64)> {
    match v? {
        Value::Array(arr) if arr.len() >= 4 => {
            let x = arr[0].as_f64()?;
            let y = arr[1].as_f64()?;
            let w = arr[2].as_f64()?;
            let h = arr[3].as_f64()?;
            Some((x, y, w, h))
        }
        Value::Object(obj) => {
            let x = obj.get("x").and_then(Value::as_f64)?;
            let y = obj.get("y").and_then(Value::as_f64)?;
            let w = obj
                .get("width")
                .or_else(|| obj.get("w"))
                .and_then(Value::as_f64)?;
            let h = obj
                .get("height")
                .or_else(|| obj.get("h"))
                .and_then(Value::as_f64)?;
            Some((x, y, w, h))
        }
        _ => None,
    }
}

/// Forward discriminated NativeActionRequest strictly for allowed native actions.
async fn handle_native(
    session: &mut PipeSession,
    client: &BrowserClient,
    id: Value,
    req: &Map<String, Value>,
) -> PipeResponse {
    let action_val = req.get("action");
    let kind = match action_val {
        Some(Value::Object(obj)) => obj
            .get("kind")
            .or_else(|| obj.get("type"))
            .and_then(Value::as_str),
        Some(Value::String(s)) => Some(s.as_str()),
        _ => req
            .get("kind")
            .or_else(|| req.get("type"))
            .and_then(Value::as_str),
    };

    let kind = match kind {
        Some(k @ ("click" | "type" | "key")) => k,
        _ => {
            return PipeResponse::error(
                id,
                "invalid_params",
                "native op requires allowed kind: 'click', 'type', or 'key'",
                None,
            );
        }
    };

    let frame_token = req
        .get("frame_token")
        .or_else(|| action_val.and_then(|a| a.get("frame_token")))
        .and_then(Value::as_str);

    let frame_token = match frame_token {
        Some(ft) if !ft.trim().is_empty() => ft,
        _ => {
            return PipeResponse::error(
                id,
                "invalid_params",
                "Missing required 'frame_token'",
                None,
            );
        }
    };

    let act_obj = action_val.and_then(Value::as_object);
    let mut args = Map::new();
    args.insert(
        "frame_token".to_string(),
        Value::String(frame_token.to_string()),
    );

    match kind {
        "click" => {
            let click_point = req
                .get("click_point_css")
                .or_else(|| req.get("click_point"))
                .or_else(|| act_obj.and_then(|a| a.get("click_point_css")))
                .or_else(|| act_obj.and_then(|a| a.get("click_point")));

            let target_rect = req
                .get("target_rect_css")
                .or_else(|| req.get("target_rect"))
                .or_else(|| act_obj.and_then(|a| a.get("target_rect_css")))
                .or_else(|| act_obj.and_then(|a| a.get("target_rect")));

            let (cx, cy) = match parse_point_f64(click_point) {
                Some(pt) if pt.0.is_finite() && pt.1.is_finite() => pt,
                _ => {
                    return PipeResponse::error(
                        id,
                        "invalid_params",
                        "click requires valid finite 'click_point_css' [x, y]",
                        None,
                    );
                }
            };

            let (rx, ry, rw, rh) = match parse_rect_f64(target_rect) {
                Some(r)
                    if r.0.is_finite()
                        && r.1.is_finite()
                        && r.2.is_finite()
                        && r.3.is_finite()
                        && r.2 > 0.0
                        && r.3 > 0.0 =>
                {
                    r
                }
                _ => {
                    return PipeResponse::error(
                        id,
                        "invalid_params",
                        "click requires valid finite 'target_rect_css' with positive width and height",
                        None,
                    );
                }
            };

            if cx < rx || cx > rx + rw || cy < ry || cy > ry + rh {
                return PipeResponse::error(
                    id,
                    "invalid_params",
                    "target_rect must contain click_point",
                    None,
                );
            }

            let mut action_map = Map::new();
            action_map.insert("kind".to_string(), Value::String("click".to_string()));
            action_map.insert("click_point_css".to_string(), json!([cx, cy]));
            action_map.insert("target_rect_css".to_string(), json!([rx, ry, rw, rh]));
            if let Some(motion) = act_obj
                .and_then(|a| a.get("motion_profile"))
                .or_else(|| req.get("motion_profile"))
            {
                action_map.insert("motion_profile".to_string(), motion.clone());
            }
            args.insert("action".to_string(), Value::Object(action_map));
        }
        "type" => {
            let text = req
                .get("text")
                .or_else(|| act_obj.and_then(|a| a.get("text")))
                .and_then(Value::as_str);
            match text {
                Some(t) if !t.is_empty() => {
                    let mut action_map = Map::new();
                    action_map.insert("kind".to_string(), Value::String("type".to_string()));
                    action_map.insert("text".to_string(), Value::String(t.to_string()));
                    args.insert("action".to_string(), Value::Object(action_map));
                    args.insert("text".to_string(), Value::String(t.to_string()));
                }
                _ => {
                    return PipeResponse::error(
                        id,
                        "invalid_params",
                        "type requires non-empty 'text'",
                        None,
                    );
                }
            }
        }
        "key" => {
            let key = req
                .get("key")
                .or_else(|| act_obj.and_then(|a| a.get("key")))
                .and_then(Value::as_str);
            match key {
                Some(k) if !k.is_empty() => {
                    let mut action_map = Map::new();
                    action_map.insert("kind".to_string(), Value::String("key".to_string()));
                    action_map.insert("key".to_string(), Value::String(k.to_string()));
                    let modifiers = req
                        .get("modifiers")
                        .or_else(|| act_obj.and_then(|a| a.get("modifiers")));
                    if let Some(m) = modifiers {
                        action_map.insert("modifiers".to_string(), m.clone());
                        args.insert("modifiers".to_string(), m.clone());
                    }
                    args.insert("action".to_string(), Value::Object(action_map));
                    args.insert("key".to_string(), Value::String(k.to_string()));
                }
                _ => {
                    return PipeResponse::error(
                        id,
                        "invalid_params",
                        "key requires non-empty 'key'",
                        None,
                    );
                }
            }
        }
        _ => unreachable!(),
    }

    for key in [
        "tab_id",
        "lease_epoch",
        "document_epoch",
        "user_modifiers_active",
        "lease",
    ] {
        if let Some(val) = req.get(key) {
            args.insert(key.to_string(), val.clone());
        }
    }

    match client
        .call_capability_with_timeout(
            "input.native_action",
            Value::Object(args),
            crate::browser::tool_timeout("input.native_action"),
        )
        .await
    {
        Ok(raw) => {
            let res = unwrap_mcp_result(raw);
            update_snapshot_token(session, &res);
            PipeResponse::success(id, res)
        }
        Err(err) => PipeResponse::bridge_error(id, &err),
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
        .call_capability_with_timeout(
            "page.accessibility_snapshot_v2",
            Value::Object(args),
            crate::browser::tool_timeout("page.accessibility_snapshot_v2"),
        )
        .await
    {
        Ok(raw) => {
            let res = unwrap_mcp_result(raw);
            update_snapshot_token(session, &res);
            PipeResponse::success(id, res)
        }
        Err(err) => PipeResponse::bridge_error(id, &err),
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
                "Missing required 'locator' field for click. Example: {\"id\":1,\"op\":\"click\",\"tab_id\":1,\"locator\":{\"css\":\"button.submit\"}}",
                None,
            )
        }
    };
    let tab_id = match req.get("tab_id").and_then(Value::as_i64) {
        Some(tab_id) if tab_id != 0 => tab_id,
        _ => {
            return PipeResponse::error(
                id,
                "invalid_params",
                "Missing required 'tab_id' field for click: pipe never guesses the focused tab, so two concurrent pipes cannot click into each other's pages. Example: {\"id\":1,\"op\":\"click\",\"tab_id\":1,\"locator\":{\"css\":\"button.submit\"}}",
                None,
            )
        }
    };

    let mut args = Map::new();
    args.insert("locator".to_string(), locator.clone());
    let observe_mode = req.get("observe").and_then(Value::as_str).unwrap_or("diff");
    args.insert(
        "observe".to_string(),
        Value::String(observe_mode.to_string()),
    );
    args.insert("lease".to_string(), Value::String("scoped".to_string()));
    if let Some(wait) = req.get("wait") {
        args.insert("wait".to_string(), wait.clone());
    }
    args.insert(
        "tab_id".to_string(),
        Value::Number(tab_id.into()),
    );

    match client
        .call_capability_with_timeout(
            "input.locator_click",
            Value::Object(args),
            crate::browser::tool_timeout("input.locator_click"),
        )
        .await
    {
        Ok(raw) => {
            let res = unwrap_mcp_result(raw);
            update_snapshot_token(session, &res);
            PipeResponse::success(id, res)
        }
        Err(err) => PipeResponse::bridge_error(id, &err),
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
                "Missing required 'locator' field for type. Example: {\"id\":1,\"op\":\"type\",\"tab_id\":1,\"locator\":{\"css\":\"input\"},\"text\":\"hello\"}",
                None,
            )
        }
    };
    let tab_id = match req.get("tab_id").and_then(Value::as_i64) {
        Some(tab_id) if tab_id != 0 => tab_id,
        _ => {
            return PipeResponse::error(
                id,
                "invalid_params",
                "Missing required 'tab_id' field for type: pipe never guesses the focused tab. Example: {\"id\":1,\"op\":\"type\",\"tab_id\":1,\"locator\":{\"css\":\"input\"},\"text\":\"hello\"}",
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
    let observe_mode = req.get("observe").and_then(Value::as_str).unwrap_or("diff");
    args.insert(
        "observe".to_string(),
        Value::String(observe_mode.to_string()),
    );
    args.insert("lease".to_string(), Value::String("scoped".to_string()));
    if let Some(wait) = req.get("wait") {
        args.insert("wait".to_string(), wait.clone());
    }
    args.insert(
        "tab_id".to_string(),
        Value::Number(tab_id.into()),
    );

    match client
        .call_capability_with_timeout(
            "input.locator_type",
            Value::Object(args),
            crate::browser::tool_timeout("input.locator_type"),
        )
        .await
    {
        Ok(raw) => {
            let res = unwrap_mcp_result(raw);
            update_snapshot_token(session, &res);
            PipeResponse::success(id, res)
        }
        Err(err) => PipeResponse::bridge_error(id, &err),
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

    // The browser exposes this capability on the control plane only; a
    // public `tools/call` is rejected with -32601.
    match client
        .call_without_reconnect_with_timeout(
            "maho/control/call",
            json!({
                "name": "browser_grant_exact_origin",
                "arguments": { "origin": origin },
            }),
            crate::browser::tool_timeout("browser_grant_exact_origin"),
        )
        .await
    {
        Ok(raw) => {
            let _granted = unwrap_mcp_result(raw);
            PipeResponse::success(id, json!({ "granted": true, "origin": origin }))
        }
        Err(err) => PipeResponse::bridge_error(id, &err),
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
    let tab_id = match req.get("tab_id").and_then(Value::as_i64) {
        Some(tab_id) if tab_id != 0 => tab_id,
        _ => {
            return PipeResponse::error(
                id,
                "invalid_params",
                "Missing required 'tab_id' field for navigate: pipe never guesses the focused tab. Example: {\"id\":1,\"op\":\"navigate\",\"tab_id\":1,\"url\":\"https://example.com\"}",
                None,
            )
        }
    };

    let mut args = Map::new();
    args.insert("url".to_string(), Value::String(url.to_string()));
    args.insert(
        "tab_id".to_string(),
        Value::Number(tab_id.into()),
    );

    if let Err(err) = crate::browser::send_control_call(
        client,
        "browser_acquire_lease",
        json!({"tab_id": tab_id, "ttl_seconds": 60}),
    )
    .await
    {
        return PipeResponse::error(id, "lease_required", err.to_string(), None);
    }

    // The browser session dispatches navigate/wait/tab-list by MCP tool name
    // only; their canonical ids (navigation.navigate, navigation.wait,
    // page.wait_for_selector, tab.list) fall through to -32601 "unknown tool".
    let nav_result = match client
        .call_capability_with_timeout(
            "browser_navigate",
            Value::Object(args),
            crate::browser::tool_timeout("browser_navigate"),
        )
        .await
    {
        Ok(raw) => unwrap_mcp_result(raw),
        Err(err) => return PipeResponse::bridge_error(id, &err),
    };

    // If wait was specified alongside navigate
    if let Some(wait_val) = req.get("wait") {
        let should_wait_nav = match wait_val {
            Value::Bool(b) => *b,
            Value::String(s) => s == "navigation" || s == "auto",
            Value::Object(map) => map
                .get("mode")
                .and_then(Value::as_str)
                .map(|m| m == "navigation" || m == "auto")
                .unwrap_or(true),
            _ => false,
        };

        if should_wait_nav {
            let timeout_ms = match wait_val {
                Value::Object(map) => map
                    .get("timeout_ms")
                    .and_then(Value::as_u64)
                    .unwrap_or(30000),
                _ => 30000,
            };
            let mut wait_args = Map::new();
            wait_args.insert("timeout_ms".to_string(), Value::Number(timeout_ms.into()));
            if let Some(tab_id) = req.get("tab_id").and_then(Value::as_i64) {
                wait_args.insert("tab_id".to_string(), Value::Number(tab_id.into()));
            }
            let _ = client
                .call_capability_with_timeout(
                    "browser_wait_for_navigation",
                    Value::Object(wait_args),
                    crate::browser::tool_timeout("browser_wait_for_navigation"),
                )
                .await;
        } else if let Value::Object(map) = wait_val {
            if let Some(sel) = map.get("selector").and_then(Value::as_str) {
                let timeout_ms = map
                    .get("timeout_ms")
                    .and_then(Value::as_u64)
                    .unwrap_or(10000);
                let mut sel_args = Map::new();
                sel_args.insert("selector".to_string(), Value::String(sel.to_string()));
                sel_args.insert("timeout_ms".to_string(), Value::Number(timeout_ms.into()));
                if let Some(tab_id) = req.get("tab_id").and_then(Value::as_i64) {
                    sel_args.insert("tab_id".to_string(), Value::Number(tab_id.into()));
                }
                let _ = client
                    .call_capability_with_timeout(
                        "page_wait_for_selector",
                        Value::Object(sel_args),
                        crate::browser::tool_timeout("page_wait_for_selector"),
                    )
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
    let timeout_ms = req
        .get("timeout_ms")
        .and_then(Value::as_u64)
        .unwrap_or(10000);

    if let Some(sel) = selector {
        let mut args = Map::new();
        args.insert("selector".to_string(), Value::String(sel.to_string()));
        args.insert("timeout_ms".to_string(), Value::Number(timeout_ms.into()));
        if let Some(tab_id) = req.get("tab_id").and_then(Value::as_i64) {
            args.insert("tab_id".to_string(), Value::Number(tab_id.into()));
        }

        match client
            .call_capability_with_timeout(
                "page_wait_for_selector",
                Value::Object(args),
                crate::browser::tool_timeout("page_wait_for_selector"),
            )
            .await
        {
            Ok(raw) => {
                let res = unwrap_mcp_result(raw);
                PipeResponse::success(id, res)
            }
            Err(err) => PipeResponse::bridge_error(id, &err),
        }
    } else if let Some(pattern) = url_pattern {
        let start = std::time::Instant::now();
        let timeout = Duration::from_millis(timeout_ms);
        let mut matched_url: Option<String> = None;

        while start.elapsed() < timeout {
            let tab_list_res = client
                .call_capability_with_timeout(
                    "browser_tab_list",
                    json!({}),
                    crate::browser::tool_timeout("browser_tab_list"),
                )
                .await;
            if let Ok(raw) = tab_list_res {
                let res = unwrap_mcp_result(raw);
                if let Some(tabs) = res.get("tabs").and_then(Value::as_array) {
                    let target_tab = if let Some(tab_id) = req.get("tab_id").and_then(Value::as_i64)
                    {
                        tabs.iter()
                            .find(|t| t.get("id") == Some(&Value::Number(tab_id.into())))
                    } else {
                        tabs.iter()
                            .find(|t| t.get("is_active") == Some(&Value::Bool(true)))
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

async fn handle_upload(
    session: &mut PipeSession,
    client: &BrowserClient,
    id: Value,
    req: &Map<String, Value>,
) -> PipeResponse {
    let tab_id = match req.get("tab_id").and_then(Value::as_i64) {
        Some(tab_id) if tab_id != 0 => tab_id,
        _ => {
            return PipeResponse::error(
                id,
                "invalid_params",
                "Missing required 'tab_id' field for upload: pipe never guesses the focused tab. Example: {\"id\":1,\"op\":\"upload\",\"tab_id\":1,\"path\":\"/path/to/file\"}",
                None,
            );
        }
    };

    let path = match req.get("path").and_then(Value::as_str) {
        Some(p) if !p.trim().is_empty() => p.trim(),
        _ => {
            return PipeResponse::error(
                id,
                "invalid_params",
                "Missing required 'path' field for upload. Example: {\"id\":1,\"op\":\"upload\",\"tab_id\":1,\"path\":\"/path/to/file\"}",
                None,
            );
        }
    };

    let selector = match req.get("selector") {
        Some(Value::String(s)) if !s.trim().is_empty() => Some(s.clone()),
        Some(Value::Null) | None => None,
        _ => {
            return PipeResponse::error(
                id,
                "invalid_params",
                "Invalid 'selector' field: must be a CSS selector string",
                None,
            );
        }
    };

    let mut upload_args = Map::new();
    upload_args.insert("path".to_string(), Value::String(path.to_string()));
    upload_args.insert("tab_id".to_string(), Value::Number(tab_id.into()));
    if let Some(sel) = selector {
        upload_args.insert("selector".to_string(), Value::String(sel));
    }

    if let Err(err) = crate::browser::send_control_call(
        client,
        "browser_acquire_lease",
        serde_json::json!({"tab_id": tab_id, "ttl_seconds": 60}),
    )
        .await
    {
        return PipeResponse::error(id, "lease_required", err.to_string(), None);
    }

    match client
        .call_capability_with_timeout(
            "browser_file_upload_select",
            Value::Object(upload_args),
            crate::browser::tool_timeout("browser_file_upload_select"),
        )
        .await
    {
        Ok(raw) => {
            let res = unwrap_mcp_result(raw);
            update_snapshot_token(session, &res);
            PipeResponse::success(id, res)
        }
        Err(err) => PipeResponse::bridge_error(id, &err),
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
                missing_op_message(),
                Some(pipe_protocol_hint()),
            );
        }
    };

    match op {
        "act" => handle_act(session, client, id, &obj).await,
        "native" => handle_native(session, client, id, &obj).await,
        "grant" => handle_grant(session, client, id, &obj).await,
        "observe" => handle_observe(session, client, id, &obj).await,
        "click" => handle_click(session, client, id, &obj).await,
        "type" => handle_type(session, client, id, &obj).await,
        "navigate" => handle_navigate(session, client, id, &obj).await,
        "wait" => handle_wait(session, client, id, &obj).await,
        "upload" => handle_upload(session, client, id, &obj).await,
        other => PipeResponse::error(
            id,
            "unknown_op",
            unknown_op_message(other),
            Some(pipe_protocol_hint()),
        ),
    }
}

/// Persistent pipe read-eval-print loop over NDJSON streams.
pub async fn run_pipe_loop<R, W>(reader: R, mut writer: W, client: &BrowserClient) -> Result<()>
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
    fn protocol_errors_name_valid_ops_and_an_example() {
        let missing = missing_op_message();
        let unknown = unknown_op_message("call");
        for message in [&missing, &unknown] {
            assert!(message.contains("observe"), "{message}");
            assert!(message.contains("click"), "{message}");
            assert!(message.contains("upload"), "{message}");
            assert!(message.contains(PIPE_EXAMPLE), "{message}");
        }
        assert!(unknown.contains("Unknown operation: call"));
        assert!(!unknown.contains("Unknown operation: call. Unknown"));
    }

    #[test]
    fn test_matches_url_pattern() {
        assert!(matches_url_pattern(
            "https://example.com/login",
            "example.com"
        ));
        assert!(matches_url_pattern("https://example.com/login", "*login"));
        assert!(matches_url_pattern(
            "https://example.com/login",
            "https://*.com/*"
        ));
        assert!(matches_url_pattern("https://example.com/login", "*"));
        assert!(!matches_url_pattern(
            "https://example.com/dashboard",
            "*login"
        ));
    }
}
