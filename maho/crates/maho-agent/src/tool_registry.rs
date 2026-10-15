use crate::ToolDefinition;
use maho_types::tool::{ToolPermission, ToolProvenance};

pub fn fs_write_definition() -> ToolDefinition {
    ToolDefinition {
        name: "fs_write".to_string(),
        description: "Write an artifact inside the session artifact directory".to_string(),
        parameters_schema: serde_json::from_str(
            r#"{
                "type": "object",
                "additionalProperties": false,
                "properties": {
                    "relative_path": { "type": "string" },
                    "content": { "type": "string" },
                    "mime_type": { "type": "string" }
                },
                "required": ["relative_path", "content"]
            }"#,
        )
        .unwrap_or_default(),
        provenance: ToolProvenance::BuiltinBrowser,
        sensitive: true,
        permission: ToolPermission::AutoApprove,
    }
}

pub fn web_search_definition() -> ToolDefinition {
    ToolDefinition {
        name: "web_search".to_string(),
        description: "Search the web for information using query with bounded results".to_string(),
        parameters_schema: serde_json::json!({
            "type": "object",
            "additionalProperties": false,
            "properties": {
                "query": { "type": "string" },
                "max_results": { "type": "integer" },
                "freshness_days": { "type": "integer" },
                "tier": {
                    "type": "string",
                    "enum": ["free", "premium", "enterprise"]
                }
            },
            "required": ["query"]
        }),
        provenance: ToolProvenance::BuiltinBrowser,
        sensitive: false,
        permission: ToolPermission::AutoApprove,
    }
}

pub fn image_search_definition() -> ToolDefinition {
    ToolDefinition {
        name: "image_search".to_string(),
        description: "Search for images on the web returning metadata and image URLs only"
            .to_string(),
        parameters_schema: serde_json::json!({
            "type": "object",
            "additionalProperties": false,
            "properties": {
                "query": { "type": "string" },
                "max_results": { "type": "integer" },
                "safe_search": { "type": "boolean" },
                "tier": {
                    "type": "string",
                    "enum": ["free", "premium", "enterprise"]
                }
            },
            "required": ["query"]
        }),
        provenance: ToolProvenance::BuiltinBrowser,
        sensitive: false,
        permission: ToolPermission::AutoApprove,
    }
}

pub fn notification_wait_definition() -> ToolDefinition {
    ToolDefinition {
        name: "notification_wait".to_string(),
        description: "Suspend execution and wait for an external notification or wake event"
            .to_string(),
        parameters_schema: serde_json::json!({
            "type": "object",
            "additionalProperties": false,
            "properties": {
                "sender": { "type": "string" },
                "source": { "type": "string" },
                "topic": { "type": "string" },
                "timeout_ms": { "type": "integer" }
            }
        }),
        provenance: ToolProvenance::BuiltinBrowser,
        sensitive: true,
        permission: ToolPermission::AlwaysAsk,
    }
}

pub fn introspection_definition() -> ToolDefinition {
    ToolDefinition {
        name: "session_introspection".to_string(),
        description: "Inspect active session runs, follow-up queue depth, model routing decision reason, and enabled capabilities count".to_string(),
        parameters_schema: serde_json::json!({
            "type": "object",
            "properties": {}
        }),
        provenance: ToolProvenance::BuiltinBrowser,
        sensitive: false,
        permission: ToolPermission::AutoApprove,
    }
}

pub fn skill_creator_definition() -> ToolDefinition {
    ToolDefinition {
        name: "skill_creator".to_string(),
        description: "Propose creating a reusable skill manifest from a successful run journal"
            .to_string(),
        parameters_schema: serde_json::json!({
            "type": "object",
            "properties": {
                "run_id": { "type": "string" },
                "name": { "type": "string" }
            },
            "required": ["run_id"]
        }),
        provenance: ToolProvenance::BuiltinBrowser,
        sensitive: true,
        permission: ToolPermission::AlwaysAsk,
    }
}

pub fn subagent_wait_definition() -> ToolDefinition {
    ToolDefinition {
        name: "subagent_wait".to_string(),
        description: "Wait for one or more child subagents to complete or time out".to_string(),
        parameters_schema: serde_json::json!({
            "type": "object",
            "additionalProperties": false,
            "properties": {
                "subagent_ids": {
                    "type": "array",
                    "items": { "type": "string" },
                    "description": "List of subagent identifiers to await"
                },
                "timeout_ms": {
                    "type": "integer",
                    "description": "Optional timeout in milliseconds"
                }
            },
            "required": ["subagent_ids"]
        }),
        provenance: ToolProvenance::BuiltinBrowser,
        sensitive: false,
        permission: ToolPermission::AutoApprove,
    }
}

/// The composite (batch) execution tool descriptor — plan row 11. The
/// registry only advertises the surface; admission is gated by the
/// session's runtime_config (fail-closed, disabled by default) and every
/// step is still authorized individually by the capability broker, so the
/// descriptor itself needs no extra approval friction.
pub fn composite_execute_definition() -> ToolDefinition {
    ToolDefinition {
        name: "composite_execute".to_string(),
        description: "Execute an ordered batch of browser page operations in one call. Steps run sequentially with per-step failure isolation: a failed step is recorded on its own result and later steps still run. Each step is authorized individually by the capability broker. Gated by the session runtime_config flag (disabled by default).".to_string(),
        parameters_schema: serde_json::from_str(
            r##"{
                "type": "object",
                "additionalProperties": false,
                "properties": {
                    "batch_id": { "type": "string" },
                    "steps": {
                        "type": "array",
                        "maxItems": 8,
                        "description": "Ordered page operations; executed sequentially.",
                        "items": {
                            "type": "object",
                            "additionalProperties": false,
                            "properties": {
                                "tool": { "type": "string" },
                                "args": { "type": "object" }
                            },
                            "required": ["tool", "args"]
                        }
                    }
                },
                "required": ["batch_id", "steps"]
            }"##,
        )
        .unwrap_or_default(),
        provenance: ToolProvenance::BuiltinBrowser,
        sensitive: false,
        permission: ToolPermission::AutoApprove,
    }
}

pub fn js_eval_definition() -> ToolDefinition {
    ToolDefinition {
        name: "js_eval".to_string(),
        description: "Evaluate a JavaScript expression with JSON input in an isolated, bounded runtime; returns JSON result and captured logs".to_string(),
        parameters_schema: serde_json::json!({
            "type": "object",
            "additionalProperties": false,
            "properties": {
                "code": { "type": "string", "maxLength": 262144 },
                "input": {}
            },
            "required": ["code"]
        }),
        provenance: ToolProvenance::BuiltinBrowser,
        sensitive: false,
        permission: ToolPermission::AutoApprove,
    }
}

pub fn artifact_create_definition() -> ToolDefinition {
    ToolDefinition {
        name: "artifact_create".to_string(),
        description: "Create a spreadsheet, document, PDF or slide deck file in the session artifact directory from structured JSON content".to_string(),
        parameters_schema: serde_json::json!({
            "type": "object",
            "additionalProperties": false,
            "properties": {
                "kind": { "type": "string", "enum": ["xlsx", "docx", "pdf", "pptx"] },
                "relative_path": { "type": "string", "description": "Single file name ending with the kind's extension" },
                "content": {
                    "type": "object",
                    "description": "xlsx: {sheets:[{name, rows}]}; docx/pdf: {blocks:[heading|paragraph|table]}; pptx: {slides:[title|bullets|image]}"
                }
            },
            "required": ["kind", "relative_path", "content"]
        }),
        provenance: ToolProvenance::BuiltinBrowser,
        sensitive: true,
        permission: ToolPermission::AutoApprove,
    }
}

/// Caps an MCP tool result: the first text item is truncated in place to keep
/// structure readable, then the whole serialized result is ALWAYS checked
/// against `output_cap` (other content items can still exceed it).
/// Shared by the desktop and iOS MCP clients.
pub(crate) fn apply_mcp_output_cap(
    mut val: serde_json::Value,
    output_cap: usize,
) -> anyhow::Result<serde_json::Value> {
    if let Some(text_val) = val.pointer_mut("/content/0/text") {
        if let Some(text) = text_val.as_str() {
            if text.len() > output_cap {
                let mut end = output_cap;
                while end > 0 && !text.is_char_boundary(end) {
                    end -= 1;
                }
                let truncated = format!("{}... [output truncated]", &text[..end]);
                *text_val = serde_json::Value::String(truncated);
            }
        }
    }

    let mut val_str = serde_json::to_string(&val)?;
    if val_str.len() > output_cap {
        let mut end = output_cap;
        while end > 0 && !val_str.is_char_boundary(end) {
            end -= 1;
        }
        val_str.truncate(end);
        return Ok(serde_json::Value::String(format!(
            "{}... [output truncated]",
            val_str
        )));
    }

    Ok(val)
}

pub fn get_platform_tools() -> Vec<ToolDefinition> {
    #[cfg(any(target_os = "ios", target_os = "android"))]
    {
        vec![
            ToolDefinition {
                name: "camera".to_string(),
                description: "Access mobile camera to take a picture".to_string(),
                parameters_schema: serde_json::json!({
                    "type": "object",
                    "properties": {}
                }),
                provenance: ToolProvenance::BuiltinBrowser,
                sensitive: false,
                permission: ToolPermission::AutoApprove,
            },
            ToolDefinition {
                name: "photo_library".to_string(),
                description: "Select a photo from mobile library".to_string(),
                parameters_schema: serde_json::json!({
                    "type": "object",
                    "properties": {}
                }),
                provenance: ToolProvenance::BuiltinBrowser,
                sensitive: false,
                permission: ToolPermission::AutoApprove,
            },
            ToolDefinition {
                name: "clipboard".to_string(),
                description: "Read or write to the mobile system clipboard".to_string(),
                parameters_schema: serde_json::json!({
                    "type": "object",
                    "properties": {
                        "action": { "type": "string", "enum": ["read", "write"] },
                        "text": { "type": "string" }
                    },
                    "required": ["action"]
                }),
                provenance: ToolProvenance::BuiltinBrowser,
                sensitive: false,
                permission: ToolPermission::AutoApprove,
            },
            web_search_definition(),
            image_search_definition(),
            notification_wait_definition(),
            introspection_definition(),
            skill_creator_definition(),
            subagent_wait_definition(),
            fs_write_definition(),
            composite_execute_definition(),
            js_eval_definition(),
        ]
    }
    #[cfg(not(any(target_os = "ios", target_os = "android")))]
    {
        vec![
            ToolDefinition {
                name: "fs_read".to_string(),
                description: "Read content of a file on desktop filesystem".to_string(),
                parameters_schema: serde_json::json!({
                    "type": "object",
                    "properties": {
                        "path": { "type": "string" }
                    },
                    "required": ["path"]
                }),
                provenance: ToolProvenance::BuiltinBrowser,
                sensitive: false,
                permission: ToolPermission::AutoApprove,
            },
            ToolDefinition {
                name: "shell_exec".to_string(),
                description: "Execute a command in desktop shell".to_string(),
                parameters_schema: serde_json::json!({
                    "type": "object",
                    "properties": {
                        "command": { "type": "string" }
                    },
                    "required": ["command"]
                }),
                provenance: ToolProvenance::BuiltinBrowser,
                sensitive: true,
                permission: ToolPermission::AlwaysAsk,
            },
            // web_fetch is intentionally NOT advertised: no runtime dispatches
            // WebFetchExecutor, so advertising it would offer the model an
            // auto-approved tool that can never execute.
            web_search_definition(),
            image_search_definition(),
            notification_wait_definition(),
            introspection_definition(),
            skill_creator_definition(),
            subagent_wait_definition(),
            fs_write_definition(),
            composite_execute_definition(),
            js_eval_definition(),
            artifact_create_definition(),
        ]
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn composite_execute_descriptor_shape_matches_contract() {
        let def = composite_execute_definition();
        assert_eq!(def.name, "composite_execute");
        assert_eq!(def.permission, ToolPermission::AutoApprove);
        assert!(!def.sensitive);
        assert_eq!(def.provenance, ToolProvenance::BuiltinBrowser);
        assert_eq!(
            def.parameters_schema["required"],
            serde_json::Value::from(vec!["batch_id", "steps"])
        );
        assert_eq!(def.parameters_schema["properties"]["steps"]["maxItems"], 8);
    }

    #[test]
    fn test_platform_tools_divergence() {
        let tools = get_platform_tools();

        #[cfg(any(target_os = "ios", target_os = "android"))]
        {
            assert_eq!(tools.len(), 12);
            assert!(tools.iter().any(|t| t.name == "camera"));
            assert!(tools.iter().any(|t| t.name == "photo_library"));
            assert!(tools.iter().any(|t| t.name == "clipboard"));
            assert!(tools.iter().any(|t| t.name == "web_search"));
            assert!(tools.iter().any(|t| t.name == "image_search"));
            assert!(tools.iter().any(|t| t.name == "notification_wait"));
            assert!(tools.iter().any(|t| t.name == "session_introspection"));
            assert!(tools.iter().any(|t| t.name == "skill_creator"));
            assert!(tools.iter().any(|t| t.name == "subagent_wait"));
            assert!(tools.iter().any(|t| t.name == "fs_write"));
            assert!(tools.iter().any(|t| t.name == "composite_execute"));
            assert!(tools.iter().any(|t| t.name == "js_eval"));
            assert!(!tools.iter().any(|t| t.name == "fs_read"));
            assert!(!tools.iter().any(|t| t.name == "shell_exec"));
        }

        #[cfg(not(any(target_os = "ios", target_os = "android")))]
        {
            assert_eq!(tools.len(), 11);
            assert!(tools.iter().any(|t| t.name == "fs_read"));
            assert!(tools.iter().any(|t| t.name == "fs_write"));
            assert!(tools.iter().any(|t| t.name == "shell_exec"));
            assert!(!tools.iter().any(|t| t.name == "web_fetch"));
            assert!(tools.iter().any(|t| t.name == "web_search"));
            assert!(tools.iter().any(|t| t.name == "image_search"));
            assert!(tools.iter().any(|t| t.name == "notification_wait"));
            assert!(tools.iter().any(|t| t.name == "session_introspection"));
            assert!(tools.iter().any(|t| t.name == "skill_creator"));
            assert!(tools.iter().any(|t| t.name == "subagent_wait"));
            assert!(tools.iter().any(|t| t.name == "composite_execute"));
            assert!(tools.iter().any(|t| t.name == "js_eval"));
            assert!(!tools.iter().any(|t| t.name == "camera"));
        }
    }

    #[test]
    fn introspection_descriptor_is_auto_approve_and_not_sensitive() {
        let def = introspection_definition();
        assert_eq!(def.name, "session_introspection");
        assert_eq!(def.permission, ToolPermission::AutoApprove);
        assert!(!def.sensitive);
        assert_eq!(def.provenance, ToolProvenance::BuiltinBrowser);
    }

    #[test]
    fn subagent_wait_descriptor_is_auto_approve_and_not_sensitive() {
        let def = subagent_wait_definition();
        assert_eq!(def.name, "subagent_wait");
        assert_eq!(def.permission, ToolPermission::AutoApprove);
        assert!(!def.sensitive);
        assert_eq!(def.provenance, ToolProvenance::BuiltinBrowser);
        assert_eq!(
            def.parameters_schema["required"],
            serde_json::json!(["subagent_ids"])
        );
    }

    #[test]
    fn skill_creator_descriptor_is_sensitive_and_always_ask() {
        let def = skill_creator_definition();
        assert_eq!(def.name, "skill_creator");
        assert_eq!(def.permission, ToolPermission::AlwaysAsk);
        assert!(def.sensitive);
        assert_eq!(def.provenance, ToolProvenance::BuiltinBrowser);
        assert_eq!(
            def.parameters_schema["required"],
            serde_json::json!(["run_id"])
        );
    }

    #[test]
    fn fs_write_is_sensitive_boundary_enforced_auto_approve() {
        let tools = get_platform_tools();
        let fs_write = tools
            .iter()
            .find(|tool| tool.name == "fs_write")
            .expect("platform registry includes fs_write");
        assert_eq!(fs_write.permission, ToolPermission::AutoApprove);
        assert_eq!(fs_write.provenance, ToolProvenance::BuiltinBrowser);
        assert!(fs_write.sensitive);
        assert_eq!(
            fs_write.parameters_schema["required"],
            serde_json::json!(["relative_path", "content"])
        );
        assert_eq!(
            fs_write.parameters_schema["properties"]["mime_type"]["type"],
            "string"
        );
    }

    #[test]
    fn web_search_descriptor_is_auto_approve_and_has_query() {
        let def = web_search_definition();
        assert_eq!(def.name, "web_search");
        assert_eq!(def.permission, ToolPermission::AutoApprove);
        assert!(!def.sensitive);
        assert_eq!(def.provenance, ToolProvenance::BuiltinBrowser);
        assert_eq!(
            def.parameters_schema["required"],
            serde_json::json!(["query"])
        );
    }

    #[test]
    fn image_search_descriptor_is_auto_approve_and_has_query() {
        let def = image_search_definition();
        assert_eq!(def.name, "image_search");
        assert_eq!(def.permission, ToolPermission::AutoApprove);
        assert!(!def.sensitive);
        assert_eq!(def.provenance, ToolProvenance::BuiltinBrowser);
        assert_eq!(
            def.parameters_schema["required"],
            serde_json::json!(["query"])
        );
    }

    #[test]
    fn notification_wait_descriptor_is_sensitive_and_always_ask() {
        let def = notification_wait_definition();
        assert_eq!(def.name, "notification_wait");
        assert_eq!(def.permission, ToolPermission::AlwaysAsk);
        assert!(def.sensitive);
        assert_eq!(def.provenance, ToolProvenance::BuiltinBrowser);
    }

    #[test]
    #[cfg(not(any(target_os = "ios", target_os = "android")))]
    fn permission_shell_exec_remains_always_ask() {
        let tools = get_platform_tools();
        let shell_exec = tools
            .iter()
            .find(|tool| tool.name == "shell_exec")
            .expect("desktop registry includes shell_exec");
        assert_eq!(shell_exec.permission, ToolPermission::AlwaysAsk);
        assert!(shell_exec.sensitive);
    }

    // No runtime dispatches WebFetchExecutor, so web_fetch must not be
    // advertised as an (auto-approved) platform tool.
    #[test]
    fn web_fetch_is_not_advertised_without_a_dispatcher() {
        assert!(!get_platform_tools()
            .iter()
            .any(|tool| tool.name == "web_fetch"));
    }

    #[test]
    fn mcp_output_cap_enforced_even_after_first_item_truncated() {
        let big = "x".repeat(200);
        let val = serde_json::json!({"content": [
            {"type": "text", "text": big.clone()},
            {"type": "text", "text": big},
        ]});
        let capped = apply_mcp_output_cap(val, 50).unwrap();
        let rendered = serde_json::to_string(&capped).unwrap();
        assert!(
            rendered.len() < 120,
            "overall cap must apply even when item 0 was truncated: {} bytes",
            rendered.len()
        );
    }
}
