// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Shared JSON-RPC 2.0 protocol types for the Maho Browser MCP transport.
//!
//! These types match the wire format defined by the C++ side in
//! `maho-chromium/browser/mcp/maho_mcp_json_rpc.h`: newline-delimited JSON
//! with standard JSON-RPC 2.0 envelopes.

use serde::{Deserialize, Serialize};
use serde_json::Value;

fn is_false(value: &bool) -> bool {
    !*value
}

/// A JSON-RPC 2.0 request ID — may be a number, a string, or null.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(untagged)]
pub enum JsonRpcId {
    /// Numeric ID (most common).
    Num(i64),
    /// String ID.
    Str(String),
    /// Null ID (rare but valid per spec).
    Null,
}

/// A JSON-RPC 2.0 request envelope.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct JsonRpcRequest {
    /// Protocol version — always "2.0".
    pub jsonrpc: String,
    /// Request identifier for correlation.
    pub id: JsonRpcId,
    /// Method name to invoke.
    pub method: String,
    /// Optional parameters.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub params: Option<Value>,
}

impl JsonRpcRequest {
    /// Create a new request with the given id, method, and params.
    pub fn new(id: i64, method: impl Into<String>, params: Option<Value>) -> Self {
        Self {
            jsonrpc: "2.0".to_string(),
            id: JsonRpcId::Num(id),
            method: method.into(),
            params,
        }
    }
}

/// A JSON-RPC 2.0 notification (no `id` field).
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct JsonRpcNotification {
    /// Protocol version — always "2.0".
    pub jsonrpc: String,
    /// Method name.
    pub method: String,
    /// Optional parameters.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub params: Option<Value>,
}

/// A JSON-RPC 2.0 response envelope.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct JsonRpcResponse {
    /// Protocol version — always "2.0".
    pub jsonrpc: String,
    /// Correlation ID matching the request. Null for error responses to
    /// un-parseable requests.
    pub id: Option<JsonRpcId>,
    /// Success payload (mutually exclusive with `error`).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub result: Option<Value>,
    /// Error payload (mutually exclusive with `result`).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<JsonRpcError>,
}

/// A JSON-RPC 2.0 error object.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct JsonRpcError {
    /// Error code (e.g., -32700 parse error, -32600 invalid request,
    /// -32601 method not found, -32602 invalid params).
    pub code: i32,
    /// Human-readable error message.
    pub message: String,
    /// Optional additional data.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub data: Option<Value>,
}

/// Parameters sent with the `initialize` request.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct InitializeParams {
    /// Protocol version string (e.g., "2025-03-26").
    pub protocol_version: String,
    /// Information about the connecting client. This is descriptive only and
    /// never grants authority or selects the browser display label.
    pub client_info: ClientInfo,
    /// Closed classification hint selected by the local transport caller.
    /// The browser honors known values only when the authenticated peer
    /// executable matches the corresponding shipped Maho executable.
    pub controller_kind: ControllerKind,
    /// Whether this connection is an autonomous agent session. This is a
    /// privilege-reducing admission mode, not an authority-bearing identity.
    #[serde(default, skip_serializing_if = "is_false")]
    pub autonomous: bool,
    /// DPAPI session token (base64-encoded, Windows only).
    /// When present, the server validates against its in-memory token.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub auth_token: Option<String>,
}

/// Client identification sent during initialization.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ClientInfo {
    /// Client name. Descriptive only; the browser does not derive authority or
    /// trusted display identity from this caller-controlled string.
    pub name: String,
    /// Client version.
    pub version: String,
}

/// Browser transport controller classification.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "kebab-case")]
pub enum ControllerKind {
    /// One-shot and scripted `maho` CLI commands.
    MahoCli,
    /// The persistent interactive `maho browser repl` session.
    MahoCliRepl,
    /// The shipped public stdio MCP bridge executable.
    MahoBrowserMcp,
    /// An arbitrary local MCP host with no trusted Maho executable identity.
    ThirdParty,
}

impl ControllerKind {
    /// Stable descriptive client name sent alongside the classification hint.
    pub const fn client_name(self) -> &'static str {
        match self {
            Self::MahoCli => "maho-cli",
            Self::MahoCliRepl => "maho-cli-repl",
            Self::MahoBrowserMcp => "maho-browser-mcp",
            Self::ThirdParty => "external-mcp",
        }
    }
}

/// Result returned from a successful `initialize` response.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct InitializeResult {
    /// Negotiated protocol version.
    pub protocol_version: String,
    /// Server identification.
    pub server_info: ServerInfoWire,
    /// Browser-owned identity bound to this transport session.
    #[serde(default)]
    pub session_info: SessionInfo,
    /// Advertised server capabilities.
    pub capabilities: Capabilities,
}

/// Browser-owned controller identity for one transport connection.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SessionInfo {
    /// Opaque, browser-generated per-connection identifier.
    pub id: String,
    /// Safe browser-selected display label.
    pub display_label: String,
    /// Browser-validated controller classification.
    pub controller_kind: ControllerKind,
    /// Browser acknowledgement of autonomous lease-admission mode.
    #[serde(default)]
    pub autonomous: bool,
}

impl Default for SessionInfo {
    fn default() -> Self {
        Self {
            id: String::new(),
            display_label: "External MCP".to_string(),
            controller_kind: ControllerKind::ThirdParty,
            autonomous: false,
        }
    }
}

/// Server identification in the wire format.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ServerInfoWire {
    /// Server name.
    pub name: String,
    /// Server version.
    pub version: String,
}

/// Server capabilities advertised during initialization.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Capabilities {
    /// Tool capabilities (present if tools are supported).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub tools: Option<Value>,
}

pub use maho_types::tool::ToolDescriptor;

// ---------------------------------------------------------------------------
// Tab tools (Wave 2.2)
// ---------------------------------------------------------------------------

/// A single browser tab entry.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct TabInfo {
    /// Browser-internal tab ID.
    pub id: i64,
    /// Stable sidebar/core tab identity when the browser exposes it.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub stable_id: Option<String>,
    /// Tab title.
    pub title: String,
    /// Tab URL.
    pub url: String,
    /// Whether this tab is the currently active/foreground tab.
    pub is_active: bool,
    /// Whether `id` can be used by action/page tools in the current session.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub targetable: Option<bool>,
    /// Chromium tab-strip index when known; `-1` for sidebar/core-only rows.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tab_strip_index: Option<i64>,
}

/// Result for `browser_tab_list`.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct TabListResult {
    /// All open tabs.
    pub tabs: Vec<TabInfo>,
}

/// Parameters for `browser_tab_get`.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TabGetParams {
    /// Tab ID to retrieve.
    pub tab_id: i64,
}

/// Parameters for `browser_navigate`.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct NavigateParams {
    /// Optional tab ID. If absent, navigates the active tab.
    pub tab_id: Option<i64>,
    /// URL to navigate to.
    pub url: String,
}

/// Parameters for `browser_tab_close`.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TabCloseParams {
    /// Tab ID to close.
    pub tab_id: i64,
}

/// Parameters for `browser_tab_new`.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TabNewParams {
    /// Optional URL to open in the new tab. Defaults to about:blank.
    pub url: Option<String>,
}

/// Result for `browser_tab_new`.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct TabNewResult {
    /// The newly created tab.
    pub tab: TabInfo,
}

// ---------------------------------------------------------------------------
// History + Bookmark tools (Wave 2.3)
// ---------------------------------------------------------------------------

/// A history entry.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct HistoryEntry {
    /// Page URL.
    pub url: String,
    /// Page title.
    pub title: String,
    /// ISO 8601 datetime of last visit.
    pub visited_at: String,
    /// Number of total visits to this URL.
    pub visit_count: i64,
}

/// Parameters for `browser_history_search`.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HistorySearchParams {
    /// Text query to search history titles/URLs.
    pub query: Option<String>,
    /// Only include entries visited within this many seconds ago.
    pub since_seconds: Option<i64>,
    /// Filter to a specific domain.
    pub domain: Option<String>,
    /// Maximum results to return (default: 20).
    pub limit: Option<i64>,
}

/// Result for `browser_history_search`.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct HistorySearchResult {
    /// Matching history entries.
    pub entries: Vec<HistoryEntry>,
}

/// A bookmark entry.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Bookmark {
    /// Bookmark ID, as stored by maho-core (a string uuid).
    pub id: String,
    /// Bookmark title.
    pub title: String,
    /// Bookmark URL.
    pub url: String,
    /// Optional folder path.
    pub folder: Option<String>,
}

/// Parameters for `browser_bookmarks_search`.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BookmarksSearchParams {
    /// Search query for bookmark titles/URLs.
    pub query: String,
}

/// Result for `browser_bookmarks_search`.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct BookmarksSearchResult {
    /// Matching bookmarks.
    pub bookmarks: Vec<Bookmark>,
}

/// Parameters for `browser_bookmark_create`.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BookmarkCreateParams {
    /// Bookmark title.
    pub title: String,
    /// Bookmark URL.
    pub url: String,
    /// Optional folder to place the bookmark in.
    pub folder: Option<String>,
}

/// Result for `browser_bookmark_create`.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct BookmarkCreateResult {
    /// The created bookmark.
    pub bookmark: Bookmark,
}

// ---------------------------------------------------------------------------
// Page content typed extractors (Wave 2.4 — OQ-2: NO evaluate_js)
// ---------------------------------------------------------------------------

/// Result for `browser_page_content`.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct PageContentResult {
    /// Extracted text content.
    pub text: String,
    /// Page URL.
    pub url: String,
    /// Page title.
    pub title: String,
    /// Whether content was redacted (e.g., sensitive fields masked).
    pub redacted: bool,
}

/// Parameters for `browser_page_text`.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PageTextParams {
    /// Optional tab ID. Defaults to the active tab.
    pub tab_id: Option<i64>,
}

/// Result for `browser_page_text`.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct PageTextResult {
    /// Plain text of the page.
    pub text: String,
}

/// Parameters for `browser_search_in_page`.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SearchInPageParams {
    /// Text query to search for within the current page.
    pub query: String,
    /// Optional tab ID. Defaults to the active tab.
    pub tab_id: Option<i64>,
}

/// Result for `browser_search_in_page`.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct SearchInPageResult {
    /// Number of matches found.
    pub match_count: i64,
    /// Index of current active match (0-based), or -1 if none.
    pub active_match_index: i64,
}

/// Parameters for `browser_page_context`.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PageContextParams {
    /// Optional tab ID. Defaults to the active tab.
    pub tab_id: Option<i64>,
}

/// Result for `browser_page_context` — structured extraction.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct PageContextResult {
    /// Page URL.
    pub url: String,
    /// Page title.
    pub title: String,
    /// Structured text content (headings, body, etc.).
    pub content: String,
    /// Meta description if available.
    pub meta_description: Option<String>,
    /// Detected language.
    pub language: Option<String>,
}

/// Parameters for `page_query_selector`.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct QuerySelectorParams {
    /// CSS selector to query.
    pub selector: String,
    /// Optional tab ID. Defaults to the active tab.
    pub tab_id: Option<i64>,
}

/// Result for `page_query_selector` — returns an opaque ref handle.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct QuerySelectorResult {
    /// Opaque reference ID for subsequent `page_get_text` / `page_get_attribute` calls.
    pub ref_id: String,
    /// Text content of the matched element (may be None if empty).
    pub text: Option<String>,
    /// HTML tag name.
    pub tag: String,
}

/// Parameters for `page_get_text`.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GetTextParams {
    /// Reference ID from a previous `page_query_selector` call.
    pub ref_id: String,
}

/// Result for `page_get_text`.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct GetTextResult {
    /// Text content of the referenced element.
    pub text: String,
}

/// Parameters for `page_get_attribute`.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GetAttributeParams {
    /// Reference ID from a previous `page_query_selector` call.
    pub ref_id: String,
    /// Attribute name to retrieve.
    pub attribute: String,
}

/// Result for `page_get_attribute`.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct GetAttributeResult {
    /// Attribute value, or None if not present.
    pub value: Option<String>,
}

/// Parameters for `page_wait_for_selector`.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct WaitForSelectorParams {
    /// CSS selector to wait for.
    pub selector: String,
    /// Timeout in milliseconds (default: 5000).
    pub timeout_ms: Option<u64>,
    /// Whether to wait for the element to be visible (default: false).
    pub visible: Option<bool>,
}

/// Result for `page_wait_for_selector`.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct WaitForSelectorResult {
    /// Whether the selector was found within the timeout.
    pub found: bool,
    /// Opaque reference ID if found.
    pub ref_id: Option<String>,
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn request_roundtrip() {
        let req = JsonRpcRequest::new(42, "tools/call", Some(json!({"name": "ping"})));
        let serialized = serde_json::to_string(&req).unwrap();
        let deserialized: JsonRpcRequest = serde_json::from_str(&serialized).unwrap();
        assert_eq!(deserialized.jsonrpc, "2.0");
        assert_eq!(deserialized.id, JsonRpcId::Num(42));
        assert_eq!(deserialized.method, "tools/call");
        assert_eq!(deserialized.params, Some(json!({"name": "ping"})));
    }

    #[test]
    fn response_success_roundtrip() {
        let resp = JsonRpcResponse {
            jsonrpc: "2.0".to_string(),
            id: Some(JsonRpcId::Num(1)),
            result: Some(json!({"pong": true})),
            error: None,
        };
        let serialized = serde_json::to_string(&resp).unwrap();
        let deserialized: JsonRpcResponse = serde_json::from_str(&serialized).unwrap();
        assert_eq!(deserialized.id, Some(JsonRpcId::Num(1)));
        assert_eq!(deserialized.result, Some(json!({"pong": true})));
        assert!(deserialized.error.is_none());
    }

    #[test]
    fn response_error_roundtrip() {
        let resp = JsonRpcResponse {
            jsonrpc: "2.0".to_string(),
            id: Some(JsonRpcId::Num(5)),
            result: None,
            error: Some(JsonRpcError {
                code: -32601,
                message: "Method not found".to_string(),
                data: None,
            }),
        };
        let serialized = serde_json::to_string(&resp).unwrap();
        let deserialized: JsonRpcResponse = serde_json::from_str(&serialized).unwrap();
        assert!(deserialized.result.is_none());
        let err = deserialized.error.unwrap();
        assert_eq!(err.code, -32601);
        assert_eq!(err.message, "Method not found");
    }

    #[test]
    fn id_variants() {
        let num: JsonRpcId = serde_json::from_str("42").unwrap();
        assert_eq!(num, JsonRpcId::Num(42));

        let s: JsonRpcId = serde_json::from_str(r#""abc""#).unwrap();
        assert_eq!(s, JsonRpcId::Str("abc".to_string()));

        let null: JsonRpcId = serde_json::from_str("null").unwrap();
        assert_eq!(null, JsonRpcId::Null);
    }

    #[test]
    fn initialize_params_roundtrip() {
        let params = InitializeParams {
            protocol_version: "2025-03-26".to_string(),
            client_info: ClientInfo {
                name: "maho-browser-mcp".to_string(),
                version: "0.1.0".to_string(),
            },
            controller_kind: ControllerKind::MahoBrowserMcp,
            autonomous: false,
            auth_token: None,
        };
        let val = serde_json::to_value(&params).unwrap();
        assert_eq!(val["protocolVersion"], "2025-03-26");
        assert_eq!(val["clientInfo"]["name"], "maho-browser-mcp");
        assert_eq!(val["controllerKind"], "maho-browser-mcp");
        assert!(val.get("autonomous").is_none());
        assert!(val.get("authToken").is_none());

        let back: InitializeParams = serde_json::from_value(val).unwrap();
        assert_eq!(back.protocol_version, "2025-03-26");
        assert_eq!(back.controller_kind, ControllerKind::MahoBrowserMcp);
        assert!(!back.autonomous);
        assert!(back.auth_token.is_none());
    }

    #[test]
    fn autonomous_initialize_params_serialize_true() {
        let params = InitializeParams {
            protocol_version: "2025-03-26".to_string(),
            client_info: ClientInfo {
                name: "maho-cli".to_string(),
                version: "0.1.0".to_string(),
            },
            controller_kind: ControllerKind::MahoCli,
            autonomous: true,
            auth_token: None,
        };
        let value = serde_json::to_value(&params).unwrap();
        assert_eq!(value["autonomous"], true);
        let roundtrip: InitializeParams = serde_json::from_value(value).unwrap();
        assert!(roundtrip.autonomous);
    }

    #[test]
    fn legacy_session_info_defaults_autonomous_false() {
        let info: SessionInfo = serde_json::from_value(json!({
            "id": "legacy-session",
            "displayLabel": "Maho CLI",
            "controllerKind": "maho-cli"
        }))
        .unwrap();
        assert!(!info.autonomous);
    }

    #[test]
    fn controller_kinds_have_stable_wire_names() {
        let cases = [
            (ControllerKind::MahoCli, "maho-cli"),
            (ControllerKind::MahoCliRepl, "maho-cli-repl"),
            (ControllerKind::MahoBrowserMcp, "maho-browser-mcp"),
            (ControllerKind::ThirdParty, "third-party"),
        ];
        for (kind, expected) in cases {
            assert_eq!(serde_json::to_value(kind).unwrap(), json!(expected));
        }
    }

    #[test]
    fn initialize_result_roundtrip() {
        let result = InitializeResult {
            protocol_version: "2025-03-26".to_string(),
            server_info: ServerInfoWire {
                name: "maho-browser".to_string(),
                version: "0.1.0".to_string(),
            },
            session_info: SessionInfo {
                id: "550e8400-e29b-41d4-a716-446655440000".to_string(),
                display_label: "Maho Browser MCP".to_string(),
                controller_kind: ControllerKind::MahoBrowserMcp,
                autonomous: false,
            },
            capabilities: Capabilities {
                tools: Some(json!({})),
            },
        };
        let val = serde_json::to_value(&result).unwrap();
        assert_eq!(val["serverInfo"]["name"], "maho-browser");
        assert_eq!(val["protocolVersion"], "2025-03-26");
        assert_eq!(val["sessionInfo"]["displayLabel"], "Maho Browser MCP");
        assert_eq!(val["sessionInfo"]["autonomous"], false);
        assert!(val["capabilities"]["tools"].is_object());

        let back: InitializeResult = serde_json::from_value(val).unwrap();
        assert_eq!(back.server_info.name, "maho-browser");
        assert_eq!(
            back.session_info.controller_kind,
            ControllerKind::MahoBrowserMcp
        );
        assert!(!back.session_info.autonomous);
    }

    #[test]
    fn tool_descriptor_roundtrip() {
        let td = ToolDescriptor {
            name: "browser_ping".to_string(),
            description: "Ping the browser".to_string(),
            parameters_schema: json!({"type": "object", "properties": {}}),
            provenance: maho_types::tool::ToolProvenance::BuiltinBrowser,
            sensitive: false,
            permission: maho_types::tool::ToolPermission::AutoApprove,
        };
        let val = serde_json::to_value(&td).unwrap();
        assert_eq!(val["name"], "browser_ping");
        assert_eq!(val["inputSchema"]["type"], "object");

        let back: ToolDescriptor = serde_json::from_value(val).unwrap();
        assert_eq!(back.name, "browser_ping");
    }

    #[test]
    fn notification_has_no_id() {
        let notif = JsonRpcNotification {
            jsonrpc: "2.0".to_string(),
            method: "notifications/initialized".to_string(),
            params: None,
        };
        let serialized = serde_json::to_string(&notif).unwrap();
        assert!(!serialized.contains("\"id\""));
        let val: Value = serde_json::from_str(&serialized).unwrap();
        assert!(val.get("id").is_none());
    }

    #[test]
    fn tab_info_roundtrip() {
        let tab = TabInfo {
            id: 1,
            stable_id: None,
            title: "GitHub".to_string(),
            url: "https://github.com".to_string(),
            is_active: true,
            targetable: None,
            tab_strip_index: None,
        };
        let val = serde_json::to_value(&tab).unwrap();
        assert_eq!(val["id"], 1);
        assert_eq!(val["is_active"], true);
        assert!(val.get("stable_id").is_none());
        assert!(val.get("targetable").is_none());
        assert!(val.get("tab_strip_index").is_none());
        let back: TabInfo = serde_json::from_value(val).unwrap();
        assert_eq!(back, tab);
    }

    #[test]
    fn tab_info_sidebar_metadata_roundtrip() {
        let tab = TabInfo {
            id: 0,
            stable_id: Some("stable-suspended".to_string()),
            title: "Suspended".to_string(),
            url: "https://example.com/suspended".to_string(),
            is_active: false,
            targetable: Some(false),
            tab_strip_index: Some(-1),
        };
        let val = serde_json::to_value(&tab).unwrap();
        assert_eq!(val["id"], 0);
        assert_eq!(val["stable_id"], "stable-suspended");
        assert_eq!(val["targetable"], false);
        assert_eq!(val["tab_strip_index"], -1);
        let back: TabInfo = serde_json::from_value(val).unwrap();
        assert_eq!(back, tab);
    }

    #[test]
    fn tab_info_legacy_json_defaults_optional_metadata() {
        let val = json!({
            "id": 5,
            "title": "Legacy",
            "url": "https://example.com/legacy",
            "is_active": true
        });
        let back: TabInfo = serde_json::from_value(val).unwrap();
        assert_eq!(back.id, 5);
        assert_eq!(back.stable_id, None);
        assert_eq!(back.targetable, None);
        assert_eq!(back.tab_strip_index, None);
    }

    #[test]
    fn tab_list_result_roundtrip() {
        let result = TabListResult {
            tabs: vec![
                TabInfo {
                    id: 1,
                    stable_id: Some("stable-a".into()),
                    title: "A".into(),
                    url: "https://a.com".into(),
                    is_active: true,
                    targetable: Some(true),
                    tab_strip_index: Some(0),
                },
                TabInfo {
                    id: 0,
                    stable_id: Some("stable-b".into()),
                    title: "B".into(),
                    url: "https://b.com".into(),
                    is_active: false,
                    targetable: Some(false),
                    tab_strip_index: Some(-1),
                },
            ],
        };
        let val = serde_json::to_value(&result).unwrap();
        assert_eq!(val["tabs"].as_array().unwrap().len(), 2);
        assert_eq!(val["tabs"][0]["targetable"], true);
        assert_eq!(val["tabs"][1]["id"], 0);
        assert_eq!(val["tabs"][1]["tab_strip_index"], -1);
        let back: TabListResult = serde_json::from_value(val).unwrap();
        assert_eq!(back, result);
    }

    #[test]
    fn navigate_params_roundtrip() {
        let params = NavigateParams {
            tab_id: Some(5),
            url: "https://x.com".into(),
        };
        let val = serde_json::to_value(&params).unwrap();
        assert_eq!(val["tab_id"], 5);
        let back: NavigateParams = serde_json::from_value(val).unwrap();
        assert_eq!(back.url, "https://x.com");
    }

    #[test]
    fn history_entry_roundtrip() {
        let entry = HistoryEntry {
            url: "https://rust-lang.org".into(),
            title: "Rust".into(),
            visited_at: "2026-07-01T10:00:00Z".into(),
            visit_count: 42,
        };
        let val = serde_json::to_value(&entry).unwrap();
        assert_eq!(val["visit_count"], 42);
        let back: HistoryEntry = serde_json::from_value(val).unwrap();
        assert_eq!(back, entry);
    }

    #[test]
    fn bookmark_roundtrip() {
        let bm = Bookmark {
            id: "bm-10".into(),
            title: "Docs".into(),
            url: "https://docs.rs".into(),
            folder: Some("Dev".into()),
        };
        let val = serde_json::to_value(&bm).unwrap();
        assert_eq!(val["folder"], "Dev");
        let back: Bookmark = serde_json::from_value(val).unwrap();
        assert_eq!(back, bm);
    }

    #[test]
    fn page_content_result_roundtrip() {
        let result = PageContentResult {
            text: "Hello world".into(),
            url: "https://example.com".into(),
            title: "Example".into(),
            redacted: true,
        };
        let val = serde_json::to_value(&result).unwrap();
        assert_eq!(val["redacted"], true);
        let back: PageContentResult = serde_json::from_value(val).unwrap();
        assert_eq!(back, result);
    }

    #[test]
    fn query_selector_result_roundtrip() {
        let result = QuerySelectorResult {
            ref_id: "ref_abc123".into(),
            text: Some("Click me".into()),
            tag: "button".into(),
        };
        let val = serde_json::to_value(&result).unwrap();
        assert_eq!(val["ref_id"], "ref_abc123");
        assert_eq!(val["tag"], "button");
        let back: QuerySelectorResult = serde_json::from_value(val).unwrap();
        assert_eq!(back, result);
    }

    #[test]
    fn wait_for_selector_params_roundtrip() {
        let params = WaitForSelectorParams {
            selector: "#main".into(),
            timeout_ms: Some(3000),
            visible: Some(true),
        };
        let val = serde_json::to_value(&params).unwrap();
        assert_eq!(val["timeout_ms"], 3000);
        assert_eq!(val["visible"], true);
        let back: WaitForSelectorParams = serde_json::from_value(val).unwrap();
        assert_eq!(back.selector, "#main");
    }
}
