// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Dynamic stdio MCP projection of the browser-owned capability registry.

use std::collections::HashSet;
use std::path::Path;
use std::sync::Arc;
use std::time::Duration;

use rmcp::model::{
    CallToolRequestParams, CallToolResult, CustomRequest, CustomResult, ErrorCode, JsonObject,
    ListToolsResult, Meta, PaginatedRequestParams, ServerCapabilities, ServerInfo, Tool,
    ToolAnnotations,
};
use rmcp::service::{RequestContext, RoleServer};
use rmcp::ServerHandler;
use serde_json::{json, Map, Value};

use crate::client::BrowserClient;
use crate::error::McpBridgeError;
use crate::paths::default_socket_path;
use crate::protocol::ControllerKind;
use crate::settings::{public_mcp_enabled, MCP_PUBLIC_ENABLED_KEY};

const PUBLIC_DISCOVERY_METHOD: &str = "tools/list";
const PUBLIC_CALL_METHOD: &str = "tools/call";

/// Decision returned by the standalone-MCP approval broker.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MutationDecision {
    /// Permit the operation.
    Allow,
    /// Deny the operation.
    Deny,
}

/// Approval broker consulted before mutable public capabilities and every
/// capability-administration request. The default is absent and fails closed.
pub type MutationApprovalCallback = Arc<dyn Fn(&str) -> MutationDecision + Send + Sync>;

/// Stable status used when no standalone approval broker is installed.
pub(crate) const ERR_BROKER_UNAVAILABLE: &str = "broker_unavailable";

#[derive(Clone, Debug)]
struct DiscoveredTool {
    mcp: Tool,
    requires_approval: bool,
}

#[derive(Clone, Debug)]
struct DiscoveryProjection {
    tools: Vec<DiscoveredTool>,
    diagnostics: Value,
}

/// The MCP server that bridges stdio to the authenticated browser transport.
#[derive(Clone)]
pub struct MahoBrowserMcpServer {
    client: Arc<BrowserClient>,
    mutation_approval: Option<MutationApprovalCallback>,
    /// Snapshot of the `maho.browser.mcp_public_enabled` registry setting
    /// taken at construction. Absent/false (the default) freezes public
    /// dispatch fail-closed; kPublicMcp code is retained, not deleted.
    public_mcp_enabled: bool,
}

impl MahoBrowserMcpServer {
    /// Connect to the browser as the shipped stdio bridge identity.
    pub async fn new() -> crate::error::Result<Self> {
        let client = BrowserClient::connect_as(
            &default_socket_path(),
            Duration::from_secs(90),
            ControllerKind::MahoBrowserMcp,
        )
        .await?;
        Ok(Self {
            client: Arc::new(client),
            mutation_approval: None,
            public_mcp_enabled: public_mcp_enabled(),
        })
    }

    /// Connect an explicit trusted host to a browser socket with a mutation
    /// approval broker. Standalone stdio remains fail-closed unless its host
    /// deliberately supplies this callback.
    pub async fn connect_with_approval(
        path: &Path,
        timeout: Duration,
        mutation_approval: MutationApprovalCallback,
    ) -> crate::error::Result<Self> {
        let client =
            BrowserClient::connect_as(path, timeout, ControllerKind::MahoBrowserMcp).await?;
        Ok(Self {
            client: Arc::new(client),
            mutation_approval: Some(mutation_approval),
            public_mcp_enabled: public_mcp_enabled(),
        })
    }

    #[cfg(test)]
    pub(crate) fn new_with_client(client: Arc<BrowserClient>) -> Self {
        Self {
            client,
            mutation_approval: None,
            public_mcp_enabled: public_mcp_enabled(),
        }
    }

    #[cfg(test)]
    pub(crate) fn new_with_client_and_approval(
        client: Arc<BrowserClient>,
        mutation_approval: Option<MutationApprovalCallback>,
    ) -> Self {
        Self {
            client,
            mutation_approval,
            public_mcp_enabled: public_mcp_enabled(),
        }
    }

    async fn discover_public_tools(&self) -> Result<DiscoveryProjection, rmcp::ErrorData> {
        let value = self
            .client
            .call_without_reconnect(PUBLIC_DISCOVERY_METHOD, json!({}))
            .await
            .map_err(browser_error)?;
        parse_discovery(value)
    }

    fn authorize(&self, operation: &str) -> Result<(), rmcp::ErrorData> {
        match &self.mutation_approval {
            Some(callback) if callback(operation) == MutationDecision::Allow => Ok(()),
            Some(_) => Err(rmcp::ErrorData::invalid_request(
                format!("'{operation}' denied by approval broker"),
                Some(json!({"status": "denied_by_broker"})),
            )),
            None => Err(rmcp::ErrorData::invalid_request(
                format!("'{operation}' denied: no approval broker is installed"),
                Some(json!({"status": ERR_BROKER_UNAVAILABLE})),
            )),
        }
    }

    async fn dispatch_public(
        &self,
        request: CallToolRequestParams,
    ) -> Result<CallToolResult, rmcp::ErrorData> {
        if !self.public_mcp_enabled {
            // kPublicMcp is frozen by default (default-off gate; the surface
            // code is retained, not deleted). Refusals reuse the existing
            // broker-unavailable fail-closed error path so external hosts see
            // the same closed shape as an unbrokered mutation.
            return Err(rmcp::ErrorData::invalid_request(
                format!(
                    "'{}' unavailable: public MCP dispatch is frozen by default; set {MCP_PUBLIC_ENABLED_KEY}=true in the browser registry settings to re-enable",
                    request.name
                ),
                Some(json!({"status": ERR_BROKER_UNAVAILABLE})),
            ));
        }
        let discovery = self.discover_public_tools().await?;
        let descriptor = discovery
            .tools
            .iter()
            .find(|tool| tool.mcp.name == request.name)
            .ok_or_else(|| {
                rmcp::ErrorData::new(
                    ErrorCode::METHOD_NOT_FOUND,
                    format!("unknown or unavailable browser tool '{}'", request.name),
                    Some(json!({"status": "unknown_or_gated_tool"})),
                )
            })?;

        if descriptor.requires_approval {
            self.authorize(&request.name)?;
        }

        let arguments = Value::Object(request.arguments.unwrap_or_default());
        let value = self
            .client
            .call_without_reconnect(
                PUBLIC_CALL_METHOD,
                json!({"name": request.name, "arguments": arguments}),
            )
            .await
            .map_err(browser_error)?;
        Ok(project_external_result(value))
    }
}

impl ServerHandler for MahoBrowserMcpServer {
    fn get_info(&self) -> ServerInfo {
        ServerInfo::new(ServerCapabilities::builder().enable_tools().build())
            .with_instructions(
                "Catalog diagnostics are browser-authoritative and exposed in tools/list _meta; control-plane capabilities are intentionally excluded.",
            )
    }

    async fn list_tools(
        &self,
        _request: Option<PaginatedRequestParams>,
        _context: RequestContext<RoleServer>,
    ) -> Result<ListToolsResult, rmcp::ErrorData> {
        let discovery = self.discover_public_tools().await?;
        let mut meta = JsonObject::new();
        meta.insert("catalogDiagnostics".to_string(), discovery.diagnostics);
        Ok(ListToolsResult {
            tools: discovery.tools.into_iter().map(|tool| tool.mcp).collect(),
            next_cursor: None,
            meta: Some(Meta(meta)),
        })
    }

    async fn call_tool(
        &self,
        request: CallToolRequestParams,
        _context: RequestContext<RoleServer>,
    ) -> Result<CallToolResult, rmcp::ErrorData> {
        self.dispatch_public(request).await
    }

    fn get_tool(&self, _name: &str) -> Option<Tool> {
        // Discovery is asynchronous. Validation happens against the same
        // browser-owned projection immediately before every dispatch.
        None
    }

    async fn on_custom_request(
        &self,
        request: CustomRequest,
        _context: RequestContext<RoleServer>,
    ) -> Result<CustomResult, rmcp::ErrorData> {
        Err(rmcp::ErrorData::new(
            ErrorCode::METHOD_NOT_FOUND,
            request.method,
            None,
        ))
    }
}

fn project_external_result(value: Value) -> CallToolResult {
    let receipt = serde_json::from_value::<maho_types::tool::BrowserToolExecution>(value.clone())
        .ok()
        .map(|execution| execution.receipt.project())
        .or_else(|| {
            value
                .get("receipt")
                .cloned()
                .and_then(|wire| serde_json::from_value(wire).ok())
                .map(|receipt: maho_types::tool::BrowserToolExecutionReceipt| receipt.project())
        });
    let mut result = CallToolResult::structured(value);
    if let Some(receipt) = receipt {
        let receipt = serde_json::to_value(receipt).expect("receipt projection serializes");
        result = result.with_meta(Some(Meta(Map::from_iter([(
            "mahoReceipt".to_string(),
            receipt,
        )]))));
    }
    result
}

fn browser_error(error: McpBridgeError) -> rmcp::ErrorData {
    match error {
        McpBridgeError::Rpc {
            code,
            message,
            data,
        } => rmcp::ErrorData::new(ErrorCode(code), message, data),
        other => rmcp::ErrorData::internal_error(other.to_string(), None),
    }
}

fn parse_discovery(value: Value) -> Result<DiscoveryProjection, rmcp::ErrorData> {
    let diagnostics = value
        .get("catalogDiagnostics")
        .or_else(|| value.get("catalog_diagnostics"))
        .cloned()
        .ok_or_else(|| discovery_error("browser discovery response has no catalog diagnostics"))?;
    validate_catalog_diagnostics(&diagnostics)?;
    let raw_tools = value
        .get("tools")
        .and_then(Value::as_array)
        .ok_or_else(|| discovery_error("browser discovery response has no tools array"))?;
    let mut names = HashSet::new();
    let mut capability_ids = HashSet::new();
    let mut tools = Vec::with_capacity(raw_tools.len());

    for raw in raw_tools {
        let object = raw
            .as_object()
            .ok_or_else(|| discovery_error("browser tool descriptor is not an object"))?;
        let name = required_string(object, "name")?;
        let capability_id = string_field(object, &["capabilityId", "capability_id"])
            .ok_or_else(|| discovery_error(format!("descriptor '{name}' has no capability id")))?;
        if !names.insert(name.clone()) || !capability_ids.insert(capability_id) {
            return Err(discovery_error("duplicate browser capability descriptor"));
        }

        positive_version(object, &["schemaVersion", "schema_version"], &name)?;
        positive_version(object, &["resultVersion", "result_version"], &name)?;
        if let Some(policy) = string_field(object, &["missingPolicy", "missing_policy"]) {
            if policy != "fail_closed" && policy != "fail-closed" && policy != "FailClosed" {
                return Err(discovery_error(format!(
                    "descriptor '{name}' does not fail closed"
                )));
            }
        }
        if object.get("public").and_then(Value::as_bool) == Some(false)
            || string_field(object, &["surface", "visibility"])
                .is_some_and(|surface| surface.contains("control") || surface == "internal")
        {
            return Err(discovery_error(format!(
                "non-public descriptor '{name}' appeared in public discovery"
            )));
        }

        let input_schema = object
            .get("inputSchema")
            .or_else(|| object.get("input_schema"))
            .and_then(Value::as_object)
            .cloned()
            .ok_or_else(|| {
                discovery_error(format!("descriptor '{name}' has invalid input schema"))
            })?;
        let output_schema = object
            .get("outputSchema")
            .or_else(|| object.get("resultSchema"))
            .or_else(|| object.get("output_schema"))
            .map(|schema| {
                schema.as_object().cloned().ok_or_else(|| {
                    discovery_error(format!("descriptor '{name}' has invalid result schema"))
                })
            })
            .transpose()?;
        let description = object
            .get("description")
            .and_then(Value::as_str)
            .filter(|description| !description.trim().is_empty())
            .ok_or_else(|| discovery_error(format!("descriptor '{name}' has no description")))?
            .to_string();

        let policy = object
            .get("policy")
            .and_then(Value::as_object)
            .ok_or_else(|| {
                discovery_error(format!("descriptor '{name}' has no policy metadata"))
            })?;
        let requires_approval = policy_requires_approval(policy);
        let read_only = string_field(policy, &["mutability"]).is_some_and(|value| {
            value == "read_only" || value == "read-only" || value == "ReadOnly"
        });
        let destructive = policy
            .get("changesAuthority")
            .or_else(|| policy.get("changes_authority"))
            .and_then(Value::as_bool)
            .unwrap_or(!read_only);

        let mut metadata = object
            .get("_meta")
            .and_then(Value::as_object)
            .cloned()
            .unwrap_or_default();
        for (key, value) in object {
            if !matches!(
                key.as_str(),
                "name"
                    | "description"
                    | "inputSchema"
                    | "input_schema"
                    | "outputSchema"
                    | "output_schema"
                    | "resultSchema"
                    | "_meta"
            ) {
                metadata.insert(key.clone(), value.clone());
            }
        }

        let mut tool = Tool::new(name, description, Arc::new(input_schema))
            .with_annotations(
                ToolAnnotations::new()
                    .read_only(read_only)
                    .destructive(destructive),
            )
            .with_meta(Meta(metadata));
        if let Some(schema) = output_schema {
            tool = tool.with_raw_output_schema(Arc::new(schema));
        }
        tools.push(DiscoveredTool {
            mcp: tool,
            requires_approval,
        });
    }
    let expected_count = diagnostics
        .pointer("/surfaces/publicMcp/count")
        .and_then(Value::as_u64)
        .ok_or_else(|| discovery_error("catalog diagnostics omit public MCP count"))?;
    if expected_count != tools.len() as u64 {
        return Err(discovery_error(
            "catalog diagnostics public MCP count does not match tools/list",
        ));
    }
    Ok(DiscoveryProjection { tools, diagnostics })
}

fn validate_catalog_diagnostics(diagnostics: &Value) -> Result<(), rmcp::ErrorData> {
    for field in [
        "catalogVersion",
        "schemaVersion",
        "resultVersion",
        "canonicalCount",
    ] {
        if diagnostics.get(field).and_then(Value::as_u64).unwrap_or(0) == 0 {
            return Err(discovery_error(format!(
                "catalog diagnostics have invalid {field}"
            )));
        }
    }
    if diagnostics
        .pointer("/surfaces/publicMcp/ids")
        .and_then(Value::as_array)
        .is_none()
        || diagnostics
            .pointer("/gates")
            .and_then(Value::as_object)
            .is_none()
    {
        return Err(discovery_error("catalog diagnostics are incomplete"));
    }
    let serialized = diagnostics.to_string();
    if serialized.contains("maho/control") || serialized.contains("credential_value") {
        return Err(discovery_error(
            "catalog diagnostics contain private control data",
        ));
    }
    Ok(())
}

fn policy_requires_approval(policy: &Map<String, Value>) -> bool {
    policy
        .get("requiresApproval")
        .or_else(|| policy.get("requires_approval"))
        .and_then(Value::as_bool)
        .unwrap_or(false)
        || policy
            .get("changesAuthority")
            .or_else(|| policy.get("changes_authority"))
            .and_then(Value::as_bool)
            .unwrap_or(false)
        || string_field(policy, &["mutability"]).is_some_and(|value| {
            value != "read_only" && value != "read-only" && value != "ReadOnly"
        })
        || string_field(policy, &["permission"])
            .is_some_and(|value| value == "always_ask" || value == "AlwaysAsk")
}

fn required_string(object: &JsonObject, key: &str) -> Result<String, rmcp::ErrorData> {
    object
        .get(key)
        .and_then(Value::as_str)
        .filter(|value| !value.trim().is_empty())
        .map(ToOwned::to_owned)
        .ok_or_else(|| discovery_error(format!("descriptor has no {key}")))
}

fn string_field<'a>(object: &'a JsonObject, keys: &[&str]) -> Option<&'a str> {
    keys.iter()
        .find_map(|key| object.get(*key).and_then(Value::as_str))
}

fn positive_version(object: &JsonObject, keys: &[&str], name: &str) -> Result<(), rmcp::ErrorData> {
    let valid = keys
        .iter()
        .find_map(|key| object.get(*key))
        .and_then(Value::as_u64)
        .is_some_and(|version| version > 0);
    if valid {
        Ok(())
    } else {
        Err(discovery_error(format!(
            "descriptor '{name}' has an invalid version"
        )))
    }
}

fn discovery_error(message: impl Into<String>) -> rmcp::ErrorData {
    rmcp::ErrorData::internal_error(
        message.into(),
        Some(json!({"status": "invalid_browser_discovery"})),
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    fn discovery(tools: Vec<Value>) -> Value {
        let ids: Vec<Value> = tools
            .iter()
            .map(|tool| tool["capabilityId"].clone())
            .collect();
        json!({
            "catalogDiagnostics": {
                "catalogVersion": 1,
                "schemaVersion": 1,
                "resultVersion": 1,
                "canonicalCount": 67,
                "gates": {"mailBeta": true, "routines": true, "vault": true},
                "surfaces": {"publicMcp": {"count": tools.len(), "ids": ids}},
                "intentionalExclusions": ["control_plane_authority"]
            },
            "tools": tools
        })
    }

    fn descriptor(name: &str, mutable: bool) -> Value {
        json!({
            "capabilityId": format!("test.{name}"),
            "name": name,
            "description": format!("Test {name}"),
            "inputSchema": {"type": "object", "properties": {}},
            "resultSchema": {"type": "object", "properties": {}},
            "schemaVersion": 1,
            "resultVersion": 1,
            "missingPolicy": "fail_closed",
            "policy": {
                "mutability": if mutable {"mutable"} else {"read_only"},
                "changesAuthority": false,
                "requiresApproval": mutable,
                "permission": if mutable {"always_ask"} else {"auto_approve"}
            },
            "category": "page"
        })
    }

    #[test]
    fn dynamic_projection_preserves_schema_and_metadata() {
        let discovery = parse_discovery(discovery(vec![descriptor("read_page", false)])).unwrap();
        assert_eq!(discovery.tools.len(), 1);
        let tool = &discovery.tools[0];
        assert_eq!(tool.mcp.name, "read_page");
        assert!(tool.mcp.output_schema.is_some());
        assert_eq!(
            tool.mcp.meta.as_ref().unwrap().0["capabilityId"],
            "test.read_page"
        );
        assert!(!tool.requires_approval);
    }

    #[test]
    fn mutable_policy_requires_broker() {
        let discovery = parse_discovery(discovery(vec![descriptor("act", true)])).unwrap();
        assert!(discovery.tools[0].requires_approval);
    }

    #[test]
    fn malformed_duplicate_and_control_descriptors_fail_closed() {
        let duplicate = descriptor("same", false);
        assert!(parse_discovery(discovery(vec![duplicate.clone(), duplicate])).is_err());
        let mut control = descriptor("secret", false);
        control["surface"] = json!("control");
        assert!(parse_discovery(discovery(vec![control])).is_err());
        let mut no_policy = descriptor("bad", false);
        no_policy.as_object_mut().unwrap().remove("policy");
        assert!(parse_discovery(discovery(vec![no_policy])).is_err());
    }

    #[test]
    fn control_methods_are_not_model_tools() {
        let discovery = parse_discovery(discovery(vec![descriptor("read_page", false)])).unwrap();
        assert!(discovery
            .tools
            .iter()
            .all(|tool| tool.mcp.name != "maho/control/list"));
        assert!(discovery
            .tools
            .iter()
            .all(|tool| tool.mcp.name != "maho/control/call"));
    }

    #[test]
    fn diagnostics_are_required_counted_and_private_data_free() {
        assert!(parse_discovery(json!({"tools": []})).is_err());

        let mut mismatched = discovery(vec![descriptor("read_page", false)]);
        mismatched["catalogDiagnostics"]["surfaces"]["publicMcp"]["count"] = json!(2);
        assert!(parse_discovery(mismatched).is_err());

        let parsed = parse_discovery(discovery(vec![descriptor("read_page", false)])).unwrap();
        assert_eq!(parsed.diagnostics["catalogVersion"], 1);
        assert_eq!(parsed.diagnostics["surfaces"]["publicMcp"]["count"], 1);
        assert!(!parsed.diagnostics.to_string().contains("maho/control"));
    }

    #[test]
    fn external_result_adds_optional_redacted_receipt_metadata_without_changing_payload() {
        let sentinel = "S3NTINEL-external-mcp-secret";
        let value = json!({
            "outputJson": "{\"ok\":true}",
            "receipt": {
                "capabilityId": "browser.page.read",
                "executionId": "receipt-stable-1",
                "metadata": {
                    "controller": {"name": "External MCP", "type": "remote_client", "plane": "mcp"},
                    "target": {"tabId": 7, "origin": "https://example.test"},
                    "category": "observe",
                    "sensitivity": "low",
                    "approval": "not_requested",
                    "state": "completed",
                    "timestamps": {"startedAt": 10.0, "completedAt": 11.0},
                    "password": sentinel,
                    "pageText": sentinel
                }
            }
        });
        let projected = project_external_result(value.clone());
        assert_eq!(projected.structured_content, Some(value));
        assert_eq!(
            projected.content.len(),
            1,
            "legacy hosts retain text content"
        );
        let meta = projected.meta.expect("extension metadata present");
        let receipt = &meta.0["mahoReceipt"];
        assert_eq!(receipt["schemaVersion"], 1);
        assert_eq!(receipt["resultVersion"], 1);
        assert_eq!(receipt["outcome"]["status"], "succeeded");
        let serialized = serde_json::to_string(receipt).unwrap();
        assert!(!serialized.contains(sentinel));
        assert!(!serialized.contains("pageText"));
    }

    #[test]
    fn legacy_external_result_remains_unchanged_without_typed_receipt() {
        let value = json!({"tabs": []});
        let projected = project_external_result(value.clone());
        assert_eq!(projected.structured_content, Some(value));
        assert!(projected.meta.is_none());
        assert_eq!(projected.content.len(), 1);
    }

    // Plan row 15: kPublicMcp freeze gate. Default (setting absent) must
    // refuse dispatch with the broker-unavailable fail-closed error path;
    // maho.browser.mcp_public_enabled=true must restore the pre-existing
    // dispatch path unchanged.
    #[cfg(all(test, unix))]
    mod public_mcp_gate {
        use super::*;
        use std::path::PathBuf;
        use std::time::Duration;
        use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
        use tokio::net::UnixListener;

        fn temp_socket() -> (tempfile::TempDir, PathBuf, UnixListener) {
            let dir = tempfile::tempdir().expect("tempdir");
            let sock_path = dir.path().join("gate-test.sock");
            let listener = UnixListener::bind(&sock_path).expect("bind");
            (dir, sock_path, listener)
        }

        /// Materializes a real registry DB at a fixture path with the toggle
        /// absent or seeded, mirroring how the browser stores settings.
        fn registry_fixture(dir: &tempfile::TempDir, enabled: Option<bool>) -> PathBuf {
            let db_path = dir.path().join("registry").join("maho.db");
            std::fs::create_dir_all(db_path.parent().unwrap()).expect("registry dir");
            match enabled {
                Some(true) => crate::settings::seed_setting(
                    &db_path,
                    crate::settings::MCP_PUBLIC_ENABLED_KEY,
                    "true",
                ),
                Some(false) => crate::settings::seed_setting(
                    &db_path,
                    crate::settings::MCP_PUBLIC_ENABLED_KEY,
                    "false",
                ),
                None => {
                    assert!(
                        crate::settings::open_registry_for_test(&db_path).is_some(),
                        "registry DB opens"
                    );
                }
            }
            db_path
        }

        /// Fake browser: completes the initialize handshake, then serves the
        /// public discovery payload and echoes tool calls, exactly like the
        /// pre-gate dispatch path expects.
        fn spawn_serving_browser(listener: UnixListener, tools: Vec<Value>, call_result: Value) {
            tokio::spawn(async move {
                let (stream, _) = listener.accept().await.expect("accept");
                let (reader, mut writer) = stream.into_split();
                let mut reader = BufReader::new(reader);
                let mut line = String::new();
                loop {
                    line.clear();
                    if reader.read_line(&mut line).await.unwrap_or(0) == 0 {
                        break;
                    }
                    let Ok(request) = serde_json::from_str::<Value>(&line) else {
                        continue;
                    };
                    let id = request["id"].clone();
                    let response = match request["method"].as_str().unwrap_or("") {
                        "initialize" => json!({
                            "jsonrpc": "2.0",
                            "id": id,
                            "result": {
                                "protocolVersion": "2025-03-26",
                                "serverInfo": {"name": "maho-browser", "version": "0.1.0"},
                                "capabilities": {"tools": {}}
                            }
                        }),
                        "tools/list" => json!({
                            "jsonrpc": "2.0",
                            "id": id,
                            "result": discovery(tools.clone())
                        }),
                        "tools/call" => json!({
                            "jsonrpc": "2.0",
                            "id": id,
                            "result": call_result
                        }),
                        _ => json!({
                            "jsonrpc": "2.0",
                            "id": id,
                            "error": {"code": -32601, "message": "Method not found"}
                        }),
                    };
                    if writer
                        .write_all(format!("{response}\n").as_bytes())
                        .await
                        .is_err()
                    {
                        break;
                    }
                }
            });
        }

        /// Constructs the server with `MAHO_DB_PATH` pinned to the fixture DB
        /// while the shared settings lock is held, then restores the
        /// environment so no other test observes host state.
        fn server_with_registry(client: Arc<BrowserClient>, db_path: &PathBuf) -> MahoBrowserMcpServer {
            let guard = crate::settings::SETTINGS_TEST_LOCK
                .lock()
                .unwrap_or_else(|e| e.into_inner());
            // SAFETY: env access is serialized by SETTINGS_TEST_LOCK.
            unsafe { std::env::set_var(crate::settings::ENV_DB_PATH, db_path) };
            let server = MahoBrowserMcpServer::new_with_client(client);
            // SAFETY: env access is serialized by SETTINGS_TEST_LOCK.
            unsafe { std::env::remove_var(crate::settings::ENV_DB_PATH) };
            drop(guard);
            server
        }

        #[tokio::test]
        async fn default_absent_setting_refuses_public_dispatch_with_broker_unavailable() {
            let (_sock_dir, sock_path, listener) = temp_socket();
            let registry_dir = tempfile::tempdir().expect("registry tempdir");
            let db_path = registry_fixture(&registry_dir, None);
            // The fake browser serves the full pre-gate flow on purpose: if
            // the gate is missing, dispatch SUCCEEDS and this test fails with
            // the refusal expectation below (failing-first evidence).
            spawn_serving_browser(
                listener,
                vec![descriptor("read_page", false)],
                json!({"ok": true, "served": "read_page"}),
            );
            let client = Arc::new(
                BrowserClient::connect_as(
                    &sock_path,
                    Duration::from_secs(5),
                    ControllerKind::MahoBrowserMcp,
                )
                .await
                .expect("client connects"),
            );
            let server = server_with_registry(client, &db_path);

            let error = server
                .dispatch_public(CallToolRequestParams::new("read_page"))
                .await
                .expect_err(
                    "default (setting absent) must refuse public dispatch fail-closed",
                );

            assert_eq!(
                error.code,
                ErrorCode::INVALID_REQUEST,
                "refusal reuses the broker-unavailable error class"
            );
            let data = error
                .data
                .expect("fail-closed status payload must be present");
            assert_eq!(data["status"], ERR_BROKER_UNAVAILABLE);
        }

        #[tokio::test]
        async fn enabled_setting_executes_preexisting_dispatch_path() {
            let (_sock_dir, sock_path, listener) = temp_socket();
            let registry_dir = tempfile::tempdir().expect("registry tempdir");
            let db_path = registry_fixture(&registry_dir, Some(true));
            let served = json!({"ok": true, "served": "read_page"});
            spawn_serving_browser(listener, vec![descriptor("read_page", false)], served.clone());
            let client = Arc::new(
                BrowserClient::connect_as(
                    &sock_path,
                    Duration::from_secs(5),
                    ControllerKind::MahoBrowserMcp,
                )
                .await
                .expect("client connects"),
            );
            let server = server_with_registry(client, &db_path);

            let result = server
                .dispatch_public(CallToolRequestParams::new("read_page"))
                .await
                .expect(
                    "maho.browser.mcp_public_enabled=true must restore the pre-existing dispatch path",
                );

            assert_eq!(result.structured_content, Some(served));
        }
    }
}
