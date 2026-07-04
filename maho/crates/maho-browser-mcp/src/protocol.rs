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
    /// Information about the connecting client.
    pub client_info: ClientInfo,
}

/// Client identification sent during initialization.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ClientInfo {
    /// Client name.
    pub name: String,
    /// Client version.
    pub version: String,
}

/// Result returned from a successful `initialize` response.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct InitializeResult {
    /// Negotiated protocol version.
    pub protocol_version: String,
    /// Server identification.
    pub server_info: ServerInfoWire,
    /// Advertised server capabilities.
    pub capabilities: Capabilities,
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

/// A tool descriptor as returned by `tools/list`.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ToolDescriptor {
    /// Tool name.
    pub name: String,
    /// Human-readable description.
    pub description: String,
    /// JSON Schema describing the tool's input parameters.
    pub input_schema: Value,
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
        };
        let val = serde_json::to_value(&params).unwrap();
        assert_eq!(val["protocolVersion"], "2025-03-26");
        assert_eq!(val["clientInfo"]["name"], "maho-browser-mcp");

        let back: InitializeParams = serde_json::from_value(val).unwrap();
        assert_eq!(back.protocol_version, "2025-03-26");
    }

    #[test]
    fn initialize_result_roundtrip() {
        let result = InitializeResult {
            protocol_version: "2025-03-26".to_string(),
            server_info: ServerInfoWire {
                name: "maho-browser".to_string(),
                version: "0.1.0".to_string(),
            },
            capabilities: Capabilities {
                tools: Some(json!({})),
            },
        };
        let val = serde_json::to_value(&result).unwrap();
        assert_eq!(val["serverInfo"]["name"], "maho-browser");
        assert_eq!(val["protocolVersion"], "2025-03-26");
        assert!(val["capabilities"]["tools"].is_object());

        let back: InitializeResult = serde_json::from_value(val).unwrap();
        assert_eq!(back.server_info.name, "maho-browser");
    }

    #[test]
    fn tool_descriptor_roundtrip() {
        let td = ToolDescriptor {
            name: "browser_ping".to_string(),
            description: "Ping the browser".to_string(),
            input_schema: json!({"type": "object", "properties": {}}),
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
}
