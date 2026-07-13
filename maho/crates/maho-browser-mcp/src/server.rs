// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! rmcp `ServerHandler` implementation bridging stdio MCP to the browser's UDS socket.

use std::sync::Arc;

use rmcp::{
    ServerHandler,
    handler::server::{router::tool::ToolRouter, wrapper::Parameters},
    model::{ServerCapabilities, ServerInfo},
    schemars, tool, tool_handler, tool_router,
};
use serde::Deserialize;
use serde_json::json;

use crate::client::BrowserClient;
use crate::paths::default_socket_path;

// ---------------------------------------------------------------------------
// Tool parameter structs (rmcp requires Deserialize + JsonSchema on params)
// ---------------------------------------------------------------------------

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserPingParams {}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserTabListParams {}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserTabGetParams {
    #[schemars(description = "Tab ID to retrieve")]
    pub tab_id: i64,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserNavigateParams {
    #[schemars(description = "Optional tab ID. If absent, navigates the active tab.")]
    pub tab_id: Option<i64>,
    #[schemars(description = "URL to navigate to")]
    pub url: String,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserTabCloseParams {
    #[schemars(description = "Tab ID to close")]
    pub tab_id: i64,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserTabNewParams {
    #[schemars(description = "Optional URL to open in the new tab")]
    pub url: Option<String>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserHistorySearchParams {
    #[schemars(description = "Text query to search history")]
    pub query: Option<String>,
    #[schemars(description = "Only include entries visited within this many seconds ago")]
    pub since_seconds: Option<i64>,
    #[schemars(description = "Filter to a specific domain")]
    pub domain: Option<String>,
    #[schemars(description = "Maximum results to return (default: 20)")]
    pub limit: Option<i64>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserBookmarksSearchParams {
    #[schemars(description = "Search query for bookmark titles/URLs")]
    pub query: String,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserBookmarkCreateParams {
    #[schemars(description = "Bookmark title")]
    pub title: String,
    #[schemars(description = "Bookmark URL")]
    pub url: String,
    #[schemars(description = "Optional folder to place the bookmark in")]
    pub folder: Option<String>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserPageContentParams {
    #[schemars(description = "Optional tab ID. Defaults to the active tab.")]
    pub tab_id: Option<i64>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserPageTextParams {
    #[schemars(description = "Optional tab ID. Defaults to the active tab.")]
    pub tab_id: Option<i64>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserSearchInPageParams {
    #[schemars(description = "Text query to search for within the page")]
    pub query: String,
    #[schemars(description = "Optional tab ID. Defaults to the active tab.")]
    pub tab_id: Option<i64>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserPageContextParams {
    #[schemars(description = "Optional tab ID. Defaults to the active tab.")]
    pub tab_id: Option<i64>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct PageQuerySelectorParams {
    #[schemars(description = "CSS selector to query")]
    pub selector: String,
    #[schemars(description = "Optional tab ID. Defaults to the active tab.")]
    pub tab_id: Option<i64>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct PageGetTextParams {
    #[schemars(description = "Reference ID from a previous page_query_selector call")]
    pub ref_id: String,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct PageGetAttributeParams {
    #[schemars(description = "Reference ID from a previous page_query_selector call")]
    pub ref_id: String,
    #[schemars(description = "Attribute name to retrieve")]
    pub attribute: String,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct PageWaitForSelectorParams {
    #[schemars(description = "CSS selector to wait for")]
    pub selector: String,
    #[schemars(description = "Timeout in milliseconds (default: 5000)")]
    pub timeout_ms: Option<u64>,
    #[schemars(description = "Whether to wait for the element to be visible")]
    pub visible: Option<bool>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserTabSwitchParams {
    #[schemars(description = "Tab ID to activate")]
    pub tab_id: i64,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserScreenshotFullParams {
    #[schemars(description = "Optional tab ID. Defaults to the active tab.")]
    pub tab_id: Option<i64>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserScreenshotElementParams {
    #[schemars(description = "Accessibility @ref of the element from browser_accessibility_snapshot")]
    pub r#ref: i64,
    #[schemars(description = "Optional tab ID. Defaults to the active tab.")]
    pub tab_id: Option<i64>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserAccessibilitySnapshotParams {
    #[schemars(description = "Optional tab ID. Defaults to the active tab.")]
    pub tab_id: Option<i64>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserClickParams {
    #[schemars(description = "Accessibility @ref of the element to click")]
    pub r#ref: i64,
    #[schemars(description = "Optional tab ID. Defaults to the active tab.")]
    pub tab_id: Option<i64>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserTypeParams {
    #[schemars(description = "Accessibility @ref of the input element")]
    pub r#ref: i64,
    #[schemars(description = "Text to type into the element")]
    pub text: String,
    #[schemars(description = "Optional tab ID. Defaults to the active tab.")]
    pub tab_id: Option<i64>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserSelectParams {
    #[schemars(description = "Accessibility @ref of the select element")]
    pub r#ref: i64,
    #[schemars(description = "Option value to select")]
    pub value: String,
    #[schemars(description = "Optional tab ID. Defaults to the active tab.")]
    pub tab_id: Option<i64>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserHoverParams {
    #[schemars(description = "Accessibility @ref of the element to hover")]
    pub r#ref: i64,
    #[schemars(description = "Optional tab ID. Defaults to the active tab.")]
    pub tab_id: Option<i64>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserScrollParams {
    #[schemars(description = "Scroll direction: 'up', 'down', 'left', 'right'")]
    pub direction: String,
    #[schemars(description = "Pixels to scroll (default: 300)")]
    pub pixels: Option<i64>,
    #[schemars(description = "Optional accessibility @ref to anchor the scroll origin")]
    pub r#ref: Option<i64>,
    #[schemars(description = "Optional tab ID. Defaults to the active tab.")]
    pub tab_id: Option<i64>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserKeyPressParams {
    #[schemars(description = "Key name (Enter, Tab, Escape, ArrowUp, F5, Space, alphanumeric single char, etc.)")]
    pub key: String,
    #[schemars(description = "Modifier keys: any of 'shift', 'ctrl', 'alt', 'meta'")]
    pub modifiers: Option<Vec<String>>,
    #[schemars(description = "Optional tab ID. Defaults to the active tab.")]
    pub tab_id: Option<i64>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserSetViewportSizeParams {
    #[schemars(description = "Viewport width in CSS pixels (100-4096)")]
    pub width_px: i64,
    #[schemars(description = "Viewport height in CSS pixels (100-4096)")]
    pub height_px: i64,
    #[schemars(description = "Optional tab ID. Defaults to the active tab.")]
    pub tab_id: Option<i64>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserWaitForNavigationParams {
    #[schemars(description = "Optional tab ID. Defaults to the active tab.")]
    pub tab_id: Option<i64>,
    #[schemars(description = "Only return navigation events after this timestamp (ms since epoch)")]
    pub since_timestamp_ms: Option<i64>,
    #[schemars(description = "Client hint for how long to wait; server returns immediately with current state")]
    pub timeout_ms: Option<i64>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserConsoleMessagesParams {
    #[schemars(description = "Optional tab ID. Defaults to the active tab.")]
    pub tab_id: Option<i64>,
    #[schemars(description = "Filter to specific levels: 'debug', 'info', 'log', 'warn', 'error'")]
    pub level_filter: Option<Vec<String>>,
    #[schemars(description = "Only return messages after this timestamp (ms since epoch)")]
    pub since_timestamp_ms: Option<i64>,
    #[schemars(description = "Maximum messages to return (default: 100)")]
    pub limit: Option<i64>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserNetworkCaptureParams {
    #[schemars(description = "Optional tab ID. Defaults to the active tab.")]
    pub tab_id: Option<i64>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserSetAllowedDomainsParams {
    #[schemars(description = "List of domains to allow (exact or wildcard like '*.github.com')")]
    pub domains: Vec<String>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserAcquireLeaseParams {
    #[schemars(description = "Tab ID to acquire a lease for")]
    pub tab_id: i64,
    #[schemars(description = "Lease TTL in seconds (10-300, default 60)")]
    pub ttl_seconds: Option<i64>,
    #[schemars(description = "If true, forcibly steal the lease from another session")]
    pub force_steal: Option<bool>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserHeartbeatLeaseParams {
    #[schemars(description = "Tab ID whose lease to extend")]
    pub tab_id: i64,
    #[schemars(description = "New TTL in seconds from now (default: same as acquire)")]
    pub ttl_seconds: Option<i64>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserReleaseLeaseParams {
    #[schemars(description = "Tab ID whose lease to release")]
    pub tab_id: i64,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserRoutinesListParams {}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct BrowserRoutinesRunParams {
    #[schemars(description = "Routine id (or name) to run — resolves built-ins first, then custom routines")]
    pub id: String,
}

/// The MCP server that bridges stdio (rmcp) to the browser's UDS socket.
#[derive(Debug, Clone)]
pub struct MahoBrowserMcpServer {
    client: Arc<BrowserClient>,
    tool_router: ToolRouter<Self>,
}

#[tool_router]
impl MahoBrowserMcpServer {
    pub async fn new() -> crate::error::Result<Self> {
        let path = default_socket_path();
        let client = BrowserClient::connect(&path).await?;
        Ok(Self {
            client: Arc::new(client),
            tool_router: Self::tool_router(),
        })
    }

    #[cfg(test)]
    pub(crate) fn new_with_client(client: Arc<BrowserClient>) -> Self {
        Self {
            client,
            tool_router: Self::tool_router(),
        }
    }

    // -----------------------------------------------------------------------
    // Ping (Wave 2.1)
    // -----------------------------------------------------------------------

    #[tool(description = "Ping the browser to verify the MCP bridge is operational")]
    async fn browser_ping(
        &self,
        #[allow(unused_variables)] Parameters(_params): Parameters<BrowserPingParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("ping", json!({}))
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_else(|_| r#"{"pong":true}"#.to_string()))
    }

    // -----------------------------------------------------------------------
    // Tab tools (Wave 2.2)
    // -----------------------------------------------------------------------

    #[tool(description = "List all open browser tabs with their IDs, titles, URLs, and active state")]
    async fn browser_tab_list(
        &self,
        #[allow(unused_variables)] Parameters(_params): Parameters<BrowserTabListParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_tab_list", json!({}))
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Get details of a specific tab by its ID")]
    async fn browser_tab_get(
        &self,
        Parameters(params): Parameters<BrowserTabGetParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_tab_get", json!({"tab_id": params.tab_id}))
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Navigate a tab to a URL. If tab_id is omitted, navigates the active tab.")]
    async fn browser_navigate(
        &self,
        Parameters(params): Parameters<BrowserNavigateParams>,
    ) -> Result<String, rmcp::ErrorData> {
        // TODO(#allowed-domains-D1): Reject URLs not in allowed_domains (Wave 3/5.3)
        let result = self
            .client
            .call_tool("browser_navigate",
                json!({"tab_id": params.tab_id, "url": params.url}),
            )
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Close a browser tab by its ID")]
    async fn browser_tab_close(
        &self,
        Parameters(params): Parameters<BrowserTabCloseParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_tab_close", json!({"tab_id": params.tab_id}))
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Open a new browser tab, optionally navigating to a URL")]
    async fn browser_tab_new(
        &self,
        Parameters(params): Parameters<BrowserTabNewParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_tab_new", json!({"url": params.url}))
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    // -----------------------------------------------------------------------
    // History + Bookmark tools (Wave 2.3)
    // -----------------------------------------------------------------------

    #[tool(description = "Search browser history by query, time range, or domain")]
    async fn browser_history_search(
        &self,
        Parameters(params): Parameters<BrowserHistorySearchParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_history_search",
                json!({
                    "query": params.query,
                    "since_seconds": params.since_seconds,
                    "domain": params.domain,
                    "limit": params.limit,
                }),
            )
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Search bookmarks by title or URL")]
    async fn browser_bookmarks_search(
        &self,
        Parameters(params): Parameters<BrowserBookmarksSearchParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_bookmarks_search", json!({"query": params.query}))
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Create a new bookmark with title, URL, and optional folder")]
    async fn browser_bookmark_create(
        &self,
        Parameters(params): Parameters<BrowserBookmarkCreateParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_bookmark_create",
                json!({
                    "title": params.title,
                    "url": params.url,
                    "folder": params.folder,
                }),
            )
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    // -----------------------------------------------------------------------
    // Page content typed extractors (Wave 2.4 — OQ-2: NO evaluate_js)
    // -----------------------------------------------------------------------

    #[tool(description = "Get the full text content of the current page (with sensitive field redaction)")]
    async fn browser_page_content(
        &self,
        Parameters(params): Parameters<BrowserPageContentParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_page_content", json!({"tab_id": params.tab_id}))
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Get plain text of the current page without HTML markup")]
    async fn browser_page_text(
        &self,
        Parameters(params): Parameters<BrowserPageTextParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_page_text", json!({"tab_id": params.tab_id}))
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Search for text within the current page and get match count")]
    async fn browser_search_in_page(
        &self,
        Parameters(params): Parameters<BrowserSearchInPageParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_search_in_page",
                json!({"query": params.query, "tab_id": params.tab_id}),
            )
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Get structured page context including URL, title, content, meta description, and language")]
    async fn browser_page_context(
        &self,
        Parameters(params): Parameters<BrowserPageContextParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_page_context", json!({"tab_id": params.tab_id}))
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Query the DOM for an element matching a CSS selector, returning an opaque ref handle")]
    async fn page_query_selector(
        &self,
        Parameters(params): Parameters<PageQuerySelectorParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("page_query_selector",
                json!({"selector": params.selector, "tab_id": params.tab_id}),
            )
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Get the text content of a DOM element by its reference ID")]
    async fn page_get_text(
        &self,
        Parameters(params): Parameters<PageGetTextParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("page_get_text", json!({"ref_id": params.ref_id}))
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Get a specific attribute value of a DOM element by its reference ID")]
    async fn page_get_attribute(
        &self,
        Parameters(params): Parameters<PageGetAttributeParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("page_get_attribute",
                json!({"ref_id": params.ref_id, "attribute": params.attribute}),
            )
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Wait for a DOM element matching a CSS selector to appear, with optional timeout and visibility check")]
    async fn page_wait_for_selector(
        &self,
        Parameters(params): Parameters<PageWaitForSelectorParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("page_wait_for_selector",
                json!({
                    "selector": params.selector,
                    "timeout_ms": params.timeout_ms,
                    "visible": params.visible,
                }),
            )
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Switch the active browser tab to the one identified by tab_id")]
    async fn browser_tab_switch(
        &self,
        Parameters(params): Parameters<BrowserTabSwitchParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_tab_switch", json!({"tab_id": params.tab_id}))
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Capture a full-page PNG screenshot of the tab (password fields overlaid with black rectangles)")]
    async fn browser_screenshot_full(
        &self,
        Parameters(params): Parameters<BrowserScreenshotFullParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_screenshot_full", json!({"tab_id": params.tab_id}))
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Capture a PNG screenshot of a single element identified by accessibility @ref")]
    async fn browser_screenshot_element(
        &self,
        Parameters(params): Parameters<BrowserScreenshotElementParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_screenshot_element",
                json!({"ref": params.r#ref, "tab_id": params.tab_id}),
            )
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Return the accessibility tree with @ref labels for interactive elements (feed refs to browser_click/type/select/hover/scroll)")]
    async fn browser_accessibility_snapshot(
        &self,
        Parameters(params): Parameters<BrowserAccessibilitySnapshotParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_accessibility_snapshot",
                json!({"tab_id": params.tab_id}),
            )
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Click an element identified by an @ref from a prior browser_accessibility_snapshot")]
    async fn browser_click(
        &self,
        Parameters(params): Parameters<BrowserClickParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_click", json!({"ref": params.r#ref, "tab_id": params.tab_id}))
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Type text into an input element identified by accessibility @ref")]
    async fn browser_type(
        &self,
        Parameters(params): Parameters<BrowserTypeParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_type",
                json!({"ref": params.r#ref, "text": params.text, "tab_id": params.tab_id}),
            )
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Select an option value on a <select> element identified by accessibility @ref")]
    async fn browser_select(
        &self,
        Parameters(params): Parameters<BrowserSelectParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_select",
                json!({"ref": params.r#ref, "value": params.value, "tab_id": params.tab_id}),
            )
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Hover the mouse over an element identified by accessibility @ref")]
    async fn browser_hover(
        &self,
        Parameters(params): Parameters<BrowserHoverParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_hover", json!({"ref": params.r#ref, "tab_id": params.tab_id}))
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Scroll the page (or an element identified by @ref) in the given direction by the given pixel amount")]
    async fn browser_scroll(
        &self,
        Parameters(params): Parameters<BrowserScrollParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_scroll",
                json!({
                    "direction": params.direction,
                    "pixels": params.pixels,
                    "ref": params.r#ref,
                    "tab_id": params.tab_id,
                }),
            )
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Synthesize a keyboard key press (named keys, F1-F12, Space, or single alphanumeric char) with optional modifiers")]
    async fn browser_key_press(
        &self,
        Parameters(params): Parameters<BrowserKeyPressParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_key_press",
                json!({
                    "key": params.key,
                    "modifiers": params.modifiers,
                    "tab_id": params.tab_id,
                }),
            )
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Resize the tab's viewport (CSS pixel dimensions between 100 and 4096)")]
    async fn browser_set_viewport_size(
        &self,
        Parameters(params): Parameters<BrowserSetViewportSizeParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_set_viewport_size",
                json!({
                    "width_px": params.width_px,
                    "height_px": params.height_px,
                    "tab_id": params.tab_id,
                }),
            )
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Return the last observed navigation event for the tab, or current URL if no event since since_timestamp_ms")]
    async fn browser_wait_for_navigation(
        &self,
        Parameters(params): Parameters<BrowserWaitForNavigationParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_wait_for_navigation",
                json!({
                    "tab_id": params.tab_id,
                    "since_timestamp_ms": params.since_timestamp_ms,
                    "timeout_ms": params.timeout_ms,
                }),
            )
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Return buffered console messages (log/info/warn/error) for the tab, with optional level and timestamp filters")]
    async fn browser_console_messages(
        &self,
        Parameters(params): Parameters<BrowserConsoleMessagesParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_console_messages",
                json!({
                    "tab_id": params.tab_id,
                    "level_filter": params.level_filter,
                    "since_timestamp_ms": params.since_timestamp_ms,
                    "limit": params.limit,
                }),
            )
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Start capturing network requests for the tab as a HAR log (credentials redacted before egress)")]
    async fn browser_network_start_capture(
        &self,
        Parameters(params): Parameters<BrowserNetworkCaptureParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_network_start_capture",
                json!({"tab_id": params.tab_id}),
            )
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Stop capturing network requests and return the final HAR log (credential-scrubbed)")]
    async fn browser_network_stop_capture(
        &self,
        Parameters(params): Parameters<BrowserNetworkCaptureParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_network_stop_capture",
                json!({"tab_id": params.tab_id}),
            )
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Return the current HAR log without stopping capture (credential-scrubbed)")]
    async fn browser_network_get_har(
        &self,
        Parameters(params): Parameters<BrowserNetworkCaptureParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_network_get_har", json!({"tab_id": params.tab_id}))
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Set the session-scoped allowed_domains list (exact or wildcard). MUST be called before browser_navigate.")]
    async fn browser_set_allowed_domains(
        &self,
        Parameters(params): Parameters<BrowserSetAllowedDomainsParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_set_allowed_domains",
                json!({"domains": params.domains}),
            )
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Get the session-scoped allowed_domains list.")]
    async fn browser_get_allowed_domains(
        &self,
        _params: Parameters<serde_json::Value>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_get_allowed_domains", serde_json::json!({}))
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Acquire an exclusive lease on the given tab (blocks other sessions from mutating it)")]
    async fn browser_acquire_lease(
        &self,
        Parameters(params): Parameters<BrowserAcquireLeaseParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_acquire_lease",
                json!({
                    "tab_id": params.tab_id,
                    "ttl_seconds": params.ttl_seconds,
                    "force_steal": params.force_steal,
                }),
            )
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Refresh an existing lease's TTL to prevent auto-expiry")]
    async fn browser_heartbeat_lease(
        &self,
        Parameters(params): Parameters<BrowserHeartbeatLeaseParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_heartbeat_lease",
                json!({"tab_id": params.tab_id, "ttl_seconds": params.ttl_seconds}),
            )
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Release a lease acquired by browser_acquire_lease")]
    async fn browser_release_lease(
        &self,
        Parameters(params): Parameters<BrowserReleaseLeaseParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_release_lease", json!({"tab_id": params.tab_id}))
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "List the routines the running browser exposes (built-in recipes + user-defined custom routines) from the maho_core routines engine")]
    async fn browser_routines_list(
        &self,
        #[allow(unused_variables)] Parameters(_params): Parameters<BrowserRoutinesListParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_routines_list", json!({}))
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }

    #[tool(description = "Run a routine by id via the running browser's maho_core routines engine. Tier-locked routines return a structured error (Max tier required).")]
    async fn browser_routines_run(
        &self,
        Parameters(params): Parameters<BrowserRoutinesRunParams>,
    ) -> Result<String, rmcp::ErrorData> {
        let result = self
            .client
            .call_tool("browser_routines_run", json!({"id": params.id}))
            .await
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))?;
        Ok(serde_json::to_string(&result).unwrap_or_default())
    }
}

#[tool_handler]
impl ServerHandler for MahoBrowserMcpServer {
    fn get_info(&self) -> ServerInfo {
        ServerInfo::new(ServerCapabilities::builder().enable_tools().build())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::error::McpBridgeError;
    use serde_json::json;
    use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
    use tokio::net::UnixListener;

    /// Helper: creates a mock browser server that responds to initialize + one tool call.
    /// Returns the socket path (test must keep `_dir` alive).
    struct MockBrowser {
        _dir: tempfile::TempDir,
        pub sock_path: std::path::PathBuf,
    }

    impl MockBrowser {
        /// Start a mock browser that responds to initialize, then echoes back the
        /// method name and params for any subsequent call as the result.
        async fn start() -> (Self, tokio::task::JoinHandle<()>) {
            let dir = tempfile::tempdir().unwrap();
            let sock_path = dir.path().join("test.sock");
            let listener = UnixListener::bind(&sock_path).unwrap();
            let mock = Self {
                _dir: dir,
                sock_path: sock_path.clone(),
            };

            let handle = tokio::spawn(async move {
                let (stream, _) = listener.accept().await.unwrap();
                let (reader, mut writer) = stream.into_split();
                let mut reader = BufReader::new(reader);
                let mut line = String::new();

                // Handle initialize.
                reader.read_line(&mut line).await.unwrap();
                let req: serde_json::Value = serde_json::from_str(&line).unwrap();
                let id = req["id"].as_i64().unwrap();
                let resp = json!({
                    "jsonrpc": "2.0",
                    "id": id,
                    "result": {
                        "protocolVersion": "2025-03-26",
                        "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                        "capabilities": {"tools": {}}
                    }
                });
                let mut buf = serde_json::to_vec(&resp).unwrap();
                buf.push(b'\n');
                writer.write_all(&buf).await.unwrap();

                // Echo loop: for each subsequent call, return params as result.
                loop {
                    line.clear();
                    match reader.read_line(&mut line).await {
                        Ok(0) => break,
                        Ok(_) => {
                            if let Ok(req) = serde_json::from_str::<serde_json::Value>(&line) {
                                let id = req["id"].as_i64().unwrap_or(0);
                                let wire_method = req["method"].as_str().unwrap_or("");
                                let raw_params = req.get("params").cloned().unwrap_or(json!(null));
                                // Wave 9 wraps tool calls in `tools/call`;
                                // unwrap so echoed `_method` / `_params`
                                // report the tool name and its arguments.
                                let (method, params) = if wire_method == "tools/call" {
                                    let name = req["params"]["name"].as_str().unwrap_or("").to_string();
                                    let args = req["params"].get("arguments").cloned().unwrap_or(json!({}));
                                    (name, args)
                                } else {
                                    (wire_method.to_string(), raw_params)
                                };

                                let resp = json!({
                                    "jsonrpc": "2.0",
                                    "id": id,
                                    "result": {
                                        "_method": method,
                                        "_params": params
                                    }
                                });
                                let mut buf = serde_json::to_vec(&resp).unwrap();
                                buf.push(b'\n');
                                writer.write_all(&buf).await.unwrap();
                            }
                        }
                        Err(_) => break,
                    }
                }
            });

            (mock, handle)
        }

        /// Start a mock that returns a custom result for a specific method.
        async fn start_with_result(
            method_name: &'static str,
            result: serde_json::Value,
        ) -> (Self, tokio::task::JoinHandle<()>) {
            let dir = tempfile::tempdir().unwrap();
            let sock_path = dir.path().join("test.sock");
            let listener = UnixListener::bind(&sock_path).unwrap();
            let mock = Self {
                _dir: dir,
                sock_path: sock_path.clone(),
            };

            let handle = tokio::spawn(async move {
                let (stream, _) = listener.accept().await.unwrap();
                let (reader, mut writer) = stream.into_split();
                let mut reader = BufReader::new(reader);
                let mut line = String::new();

                // Handle initialize.
                reader.read_line(&mut line).await.unwrap();
                let req: serde_json::Value = serde_json::from_str(&line).unwrap();
                let id = req["id"].as_i64().unwrap();
                let resp = json!({
                    "jsonrpc": "2.0",
                    "id": id,
                    "result": {
                        "protocolVersion": "2025-03-26",
                        "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                        "capabilities": {"tools": {}}
                    }
                });
                let mut buf = serde_json::to_vec(&resp).unwrap();
                buf.push(b'\n');
                writer.write_all(&buf).await.unwrap();

                // Handle the target call. Wave 9 wraps all tool calls in
                // `tools/call` envelope, so accept either the raw method name
                // (legacy) or `tools/call` with matching params.name.
                line.clear();
                reader.read_line(&mut line).await.unwrap();
                let req2: serde_json::Value = serde_json::from_str(&line).unwrap();
                let id2 = req2["id"].as_i64().unwrap();
                let called_method = req2["method"].as_str().unwrap_or("");
                if called_method == "tools/call" {
                    let inner_name = req2["params"]["name"].as_str().unwrap_or("");
                    assert_eq!(
                        inner_name, method_name,
                        "tools/call: expected name='{method_name}', got '{inner_name}'"
                    );
                } else {
                    assert_eq!(
                        called_method, method_name,
                        "Expected call to '{method_name}', got '{called_method}'"
                    );
                }
                let resp2 = json!({
                    "jsonrpc": "2.0",
                    "id": id2,
                    "result": result
                });
                let mut buf2 = serde_json::to_vec(&resp2).unwrap();
                buf2.push(b'\n');
                writer.write_all(&buf2).await.unwrap();
            });

            (mock, handle)
        }

        /// Start a mock that returns a JSON-RPC error for any method call.
        async fn start_with_error(
            code: i32,
            message: &'static str,
        ) -> (Self, tokio::task::JoinHandle<()>) {
            let dir = tempfile::tempdir().unwrap();
            let sock_path = dir.path().join("test.sock");
            let listener = UnixListener::bind(&sock_path).unwrap();
            let mock = Self {
                _dir: dir,
                sock_path: sock_path.clone(),
            };

            let handle = tokio::spawn(async move {
                let (stream, _) = listener.accept().await.unwrap();
                let (reader, mut writer) = stream.into_split();
                let mut reader = BufReader::new(reader);
                let mut line = String::new();

                // Handle initialize.
                reader.read_line(&mut line).await.unwrap();
                let req: serde_json::Value = serde_json::from_str(&line).unwrap();
                let id = req["id"].as_i64().unwrap();
                let resp = json!({
                    "jsonrpc": "2.0",
                    "id": id,
                    "result": {
                        "protocolVersion": "2025-03-26",
                        "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                        "capabilities": {"tools": {}}
                    }
                });
                let mut buf = serde_json::to_vec(&resp).unwrap();
                buf.push(b'\n');
                writer.write_all(&buf).await.unwrap();

                // Return error for the next call.
                line.clear();
                reader.read_line(&mut line).await.unwrap();
                let req2: serde_json::Value = serde_json::from_str(&line).unwrap();
                let id2 = req2["id"].as_i64().unwrap();
                let resp2 = json!({
                    "jsonrpc": "2.0",
                    "id": id2,
                    "error": {"code": code, "message": message}
                });
                let mut buf2 = serde_json::to_vec(&resp2).unwrap();
                buf2.push(b'\n');
                writer.write_all(&buf2).await.unwrap();
            });

            (mock, handle)
        }
    }

    // SC7: existing ping test
    #[tokio::test]
    async fn browser_ping_via_mock_client() {
        let (mock, handle) =
            MockBrowser::start_with_result("ping", json!({"pong": true})).await;
        let client = BrowserClient::connect(&mock.sock_path).await.unwrap();
        let server = MahoBrowserMcpServer::new_with_client(Arc::new(client));

        let result = server
            .browser_ping(Parameters(BrowserPingParams {}))
            .await
            .unwrap();
        let parsed: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(parsed, json!({"pong": true}));

        handle.await.unwrap();
    }

    // T1: browser_tab_list returns 3 tabs
    #[tokio::test]
    async fn t1_browser_tab_list_returns_tabs() {
        let tabs_result = json!({
            "tabs": [
                {"id": 1, "title": "Tab 1", "url": "https://a.com", "is_active": true},
                {"id": 2, "title": "Tab 2", "url": "https://b.com", "is_active": false},
                {"id": 3, "title": "Tab 3", "url": "https://c.com", "is_active": false},
            ]
        });
        let (mock, handle) =
            MockBrowser::start_with_result("browser_tab_list", tabs_result.clone()).await;
        let client = BrowserClient::connect(&mock.sock_path).await.unwrap();
        let server = MahoBrowserMcpServer::new_with_client(Arc::new(client));

        let result = server
            .browser_tab_list(Parameters(BrowserTabListParams {}))
            .await
            .unwrap();
        let parsed: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(parsed["tabs"].as_array().unwrap().len(), 3);
        assert_eq!(parsed["tabs"][0]["title"], "Tab 1");

        handle.await.unwrap();
    }

    // T2: browser_tab_get returns active tab
    #[tokio::test]
    async fn t2_browser_tab_get_returns_tab() {
        let tab_result = json!({"id": 5, "title": "Active", "url": "https://x.com", "is_active": true});
        let (mock, handle) =
            MockBrowser::start_with_result("browser_tab_get", tab_result.clone()).await;
        let client = BrowserClient::connect(&mock.sock_path).await.unwrap();
        let server = MahoBrowserMcpServer::new_with_client(Arc::new(client));

        let result = server
            .browser_tab_get(Parameters(BrowserTabGetParams { tab_id: 5 }))
            .await
            .unwrap();
        let parsed: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(parsed["id"], 5);
        assert_eq!(parsed["is_active"], true);

        handle.await.unwrap();
    }

    // T3: browser_navigate rejects missing url via -32602 from browser
    #[tokio::test]
    async fn t3_browser_navigate_propagates_error() {
        let (mock, handle) =
            MockBrowser::start_with_error(-32602, "Invalid params: url required").await;
        let client = BrowserClient::connect(&mock.sock_path).await.unwrap();
        let server = MahoBrowserMcpServer::new_with_client(Arc::new(client));

        let result = server
            .browser_navigate(Parameters(BrowserNavigateParams {
                tab_id: None,
                url: String::new(),
            }))
            .await;
        assert!(result.is_err());
        let err = result.unwrap_err();
        assert!(err.message.contains("Invalid params") || err.message.contains("-32602"));

        handle.await.unwrap();
    }

    // T4: browser_tab_close idempotent (closing missing tab → clear success)
    #[tokio::test]
    async fn t4_browser_tab_close_idempotent() {
        let (mock, handle) =
            MockBrowser::start_with_result("browser_tab_close", json!({"closed": true})).await;
        let client = BrowserClient::connect(&mock.sock_path).await.unwrap();
        let server = MahoBrowserMcpServer::new_with_client(Arc::new(client));

        let result = server
            .browser_tab_close(Parameters(BrowserTabCloseParams { tab_id: 999 }))
            .await
            .unwrap();
        let parsed: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(parsed["closed"], true);

        handle.await.unwrap();
    }

    // T5: browser_tab_new passes url through
    #[tokio::test]
    async fn t5_browser_tab_new_passes_url() {
        let (mock, handle) = MockBrowser::start().await;
        let client = BrowserClient::connect(&mock.sock_path).await.unwrap();
        let server = MahoBrowserMcpServer::new_with_client(Arc::new(client));

        let result = server
            .browser_tab_new(Parameters(BrowserTabNewParams {
                url: Some("https://new.page".into()),
            }))
            .await
            .unwrap();
        let parsed: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(parsed["_method"], "browser_tab_new");
        assert_eq!(parsed["_params"]["url"], "https://new.page");

        drop(server);
        handle.await.unwrap();
    }

    // T6: browser_history_search with since_seconds
    #[tokio::test]
    async fn t6_browser_history_search_since() {
        let (mock, handle) = MockBrowser::start().await;
        let client = BrowserClient::connect(&mock.sock_path).await.unwrap();
        let server = MahoBrowserMcpServer::new_with_client(Arc::new(client));

        let result = server
            .browser_history_search(Parameters(BrowserHistorySearchParams {
                query: Some("rust".into()),
                since_seconds: Some(604800),
                domain: None,
                limit: Some(10),
            }))
            .await
            .unwrap();
        let parsed: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(parsed["_method"], "browser_history_search");
        assert_eq!(parsed["_params"]["since_seconds"], 604800);

        drop(server);
        handle.await.unwrap();
    }

    // FIX-4-browser: browser_routines_list + browser_routines_run are exposed
    // and delegate over the bridge to the browser's (SAME) maho_core routines
    // engine — list returns the built-ins, run resolves the routine by id.
    #[tokio::test]
    async fn routines_list_run_exposed() {
        // browser_routines_list: the running browser's engine returns the merged
        // built-in + custom routine set. The bridge must forward verbatim.
        let list_result = json!({
            "routines": [
                {"id": "morning_briefing", "name": "Morning Briefing", "source": "builtin"},
                {"id": "close_old_tabs", "name": "Close Old Tabs", "source": "builtin"},
                {"id": "summarize_reading_list", "name": "Summarize Reading List", "source": "builtin"},
                {"id": "weekly_tab_tidy", "name": "Weekly Tab Tidy", "source": "builtin"},
            ]
        });
        let (mock, handle) =
            MockBrowser::start_with_result("browser_routines_list", list_result).await;
        let client = BrowserClient::connect(&mock.sock_path).await.unwrap();
        let server = MahoBrowserMcpServer::new_with_client(Arc::new(client));

        let listed = server
            .browser_routines_list(Parameters(BrowserRoutinesListParams {}))
            .await
            .unwrap();
        let parsed: serde_json::Value = serde_json::from_str(&listed).unwrap();
        let routines = parsed["routines"].as_array().unwrap();
        assert_eq!(routines.len(), 4);
        assert!(routines.iter().any(|r| r["id"] == "morning_briefing"));
        handle.await.unwrap();

        // browser_routines_run: the bridge forwards the routine id to the engine
        // (which resolves built-ins first, then custom). Echo mock confirms the
        // exact wire method + the id argument reach the engine unchanged.
        let (mock2, handle2) = MockBrowser::start().await;
        let client2 = BrowserClient::connect(&mock2.sock_path).await.unwrap();
        let server2 = MahoBrowserMcpServer::new_with_client(Arc::new(client2));

        let ran = server2
            .browser_routines_run(Parameters(BrowserRoutinesRunParams {
                id: "morning_briefing".into(),
            }))
            .await
            .unwrap();
        let parsed2: serde_json::Value = serde_json::from_str(&ran).unwrap();
        assert_eq!(parsed2["_method"], "browser_routines_run");
        assert_eq!(parsed2["_params"]["id"], "morning_briefing");

        drop(server2);
        handle2.await.unwrap();
    }

    // T7: browser_bookmarks_search — query flows through
    #[tokio::test]
    async fn t7_browser_bookmarks_search() {
        let (mock, handle) = MockBrowser::start().await;
        let client = BrowserClient::connect(&mock.sock_path).await.unwrap();
        let server = MahoBrowserMcpServer::new_with_client(Arc::new(client));

        let result = server
            .browser_bookmarks_search(Parameters(BrowserBookmarksSearchParams {
                query: "docs".into(),
            }))
            .await
            .unwrap();
        let parsed: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(parsed["_params"]["query"], "docs");

        drop(server);
        handle.await.unwrap();
    }

    // T8: browser_bookmark_create — title+url+folder flows
    #[tokio::test]
    async fn t8_browser_bookmark_create() {
        let (mock, handle) = MockBrowser::start().await;
        let client = BrowserClient::connect(&mock.sock_path).await.unwrap();
        let server = MahoBrowserMcpServer::new_with_client(Arc::new(client));

        let result = server
            .browser_bookmark_create(Parameters(BrowserBookmarkCreateParams {
                title: "Rust Docs".into(),
                url: "https://doc.rust-lang.org".into(),
                folder: Some("Dev".into()),
            }))
            .await
            .unwrap();
        let parsed: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(parsed["_params"]["title"], "Rust Docs");
        assert_eq!(parsed["_params"]["folder"], "Dev");

        drop(server);
        handle.await.unwrap();
    }

    // T9: browser_page_content — returns redacted placeholder
    #[tokio::test]
    async fn t9_browser_page_content_redacted() {
        let page_result = json!({
            "text": "[REDACTED]",
            "url": "https://bank.com/login",
            "title": "Login",
            "redacted": true
        });
        let (mock, handle) =
            MockBrowser::start_with_result("browser_page_content", page_result).await;
        let client = BrowserClient::connect(&mock.sock_path).await.unwrap();
        let server = MahoBrowserMcpServer::new_with_client(Arc::new(client));

        let result = server
            .browser_page_content(Parameters(BrowserPageContentParams { tab_id: None }))
            .await
            .unwrap();
        let parsed: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(parsed["redacted"], true);

        handle.await.unwrap();
    }

    // T10: browser_page_text — flows through
    #[tokio::test]
    async fn t10_browser_page_text() {
        let (mock, handle) = MockBrowser::start().await;
        let client = BrowserClient::connect(&mock.sock_path).await.unwrap();
        let server = MahoBrowserMcpServer::new_with_client(Arc::new(client));

        let result = server
            .browser_page_text(Parameters(BrowserPageTextParams { tab_id: Some(1) }))
            .await
            .unwrap();
        let parsed: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(parsed["_method"], "browser_page_text");

        drop(server);
        handle.await.unwrap();
    }

    // T11: browser_search_in_page — query flows
    #[tokio::test]
    async fn t11_browser_search_in_page() {
        let (mock, handle) = MockBrowser::start().await;
        let client = BrowserClient::connect(&mock.sock_path).await.unwrap();
        let server = MahoBrowserMcpServer::new_with_client(Arc::new(client));

        let result = server
            .browser_search_in_page(Parameters(BrowserSearchInPageParams {
                query: "hello".into(),
                tab_id: None,
            }))
            .await
            .unwrap();
        let parsed: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(parsed["_params"]["query"], "hello");

        drop(server);
        handle.await.unwrap();
    }

    // T12: browser_page_context — structured context flows
    #[tokio::test]
    async fn t12_browser_page_context() {
        let (mock, handle) = MockBrowser::start().await;
        let client = BrowserClient::connect(&mock.sock_path).await.unwrap();
        let server = MahoBrowserMcpServer::new_with_client(Arc::new(client));

        let result = server
            .browser_page_context(Parameters(BrowserPageContextParams { tab_id: Some(2) }))
            .await
            .unwrap();
        let parsed: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(parsed["_method"], "browser_page_context");
        assert_eq!(parsed["_params"]["tab_id"], 2);

        drop(server);
        handle.await.unwrap();
    }

    // T13: page_query_selector returns ref_id
    #[tokio::test]
    async fn t13_page_query_selector_returns_ref() {
        let selector_result = json!({
            "ref_id": "ref_001",
            "text": "Submit",
            "tag": "button"
        });
        let (mock, handle) =
            MockBrowser::start_with_result("page_query_selector", selector_result).await;
        let client = BrowserClient::connect(&mock.sock_path).await.unwrap();
        let server = MahoBrowserMcpServer::new_with_client(Arc::new(client));

        let result = server
            .page_query_selector(Parameters(PageQuerySelectorParams {
                selector: "button.submit".into(),
                tab_id: None,
            }))
            .await
            .unwrap();
        let parsed: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(parsed["ref_id"], "ref_001");
        assert_eq!(parsed["tag"], "button");

        handle.await.unwrap();
    }

    // T14: page_get_text by ref — ref flows through
    #[tokio::test]
    async fn t14_page_get_text_by_ref() {
        let (mock, handle) = MockBrowser::start().await;
        let client = BrowserClient::connect(&mock.sock_path).await.unwrap();
        let server = MahoBrowserMcpServer::new_with_client(Arc::new(client));

        let result = server
            .page_get_text(Parameters(PageGetTextParams {
                ref_id: "ref_xyz".into(),
            }))
            .await
            .unwrap();
        let parsed: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(parsed["_params"]["ref_id"], "ref_xyz");

        drop(server);
        handle.await.unwrap();
    }

    // T15: page_get_attribute — attribute name flows
    #[tokio::test]
    async fn t15_page_get_attribute() {
        let (mock, handle) = MockBrowser::start().await;
        let client = BrowserClient::connect(&mock.sock_path).await.unwrap();
        let server = MahoBrowserMcpServer::new_with_client(Arc::new(client));

        let result = server
            .page_get_attribute(Parameters(PageGetAttributeParams {
                ref_id: "ref_abc".into(),
                attribute: "href".into(),
            }))
            .await
            .unwrap();
        let parsed: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(parsed["_params"]["ref_id"], "ref_abc");
        assert_eq!(parsed["_params"]["attribute"], "href");

        drop(server);
        handle.await.unwrap();
    }

    // T16: page_wait_for_selector — timeout param flows
    #[tokio::test]
    async fn t16_page_wait_for_selector() {
        let (mock, handle) = MockBrowser::start().await;
        let client = BrowserClient::connect(&mock.sock_path).await.unwrap();
        let server = MahoBrowserMcpServer::new_with_client(Arc::new(client));

        let result = server
            .page_wait_for_selector(Parameters(PageWaitForSelectorParams {
                selector: "#loading".into(),
                timeout_ms: Some(3000),
                visible: Some(true),
            }))
            .await
            .unwrap();
        let parsed: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(parsed["_params"]["timeout_ms"], 3000);
        assert_eq!(parsed["_params"]["visible"], true);

        drop(server);
        handle.await.unwrap();
    }

    // T17: evaluate_js → -32601 (OQ-2 regression)
    #[tokio::test]
    async fn t17_evaluate_js_returns_method_not_found() {
        let (mock, handle) =
            MockBrowser::start_with_error(-32601, "Method not found").await;
        let client = BrowserClient::connect(&mock.sock_path).await.unwrap();

        let err = client.call("evaluate_js", json!({"code": "alert(1)"})).await.unwrap_err();
        match err {
            McpBridgeError::Rpc { code, .. } => assert_eq!(code, -32601),
            other => panic!("expected -32601 Rpc error, got: {other:?}"),
        }

        handle.await.unwrap();
    }
}
