//! Typed events parsed from `session/update` notifications.

use serde::{Deserialize, Serialize};
use serde_json::Value;

#[derive(Debug, Clone)]
pub enum AcpUpdate {
    AgentMessageChunk {
        message_id: String,
        text: String,
    },
    AgentThoughtChunk {
        message_id: String,
        text: String,
    },
    UserMessageChunk {
        message_id: String,
        text: String,
    },
    Plan {
        entries: Vec<PlanEntry>,
    },
    ToolCall {
        id: String,
        title: String,
        kind: ToolKind,
        status: ToolCallStatus,
        locations: Vec<ToolCallLocation>,
        raw_input: Value,
    },
    ToolCallUpdate {
        id: String,
        status: ToolCallStatus,
        content_text: Option<String>,
        raw_output: Option<Value>,
    },
    AvailableCommandsUpdate {
        commands: Vec<AvailableCommand>,
    },
    ConfigOptionUpdate {
        options: Value,
    },
    UsageUpdate {
        used: Option<u64>,
        size: Option<u64>,
        cost: Option<UsageCost>,
    },
    Other {
        method: String,
        raw_params: Value,
    },
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct UsageCost {
    pub amount: f64,
    pub currency: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PlanEntry {
    #[serde(default)]
    pub title: String,
    #[serde(default)]
    pub status: String,
    #[serde(flatten)]
    pub extra: Value,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum ToolKind {
    Tool,
    Bash,
    Read,
    Write,
    Edit,
    Glob,
    Grep,
    Fetch,
    #[serde(other)]
    Unknown,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum ToolCallStatus {
    Pending,
    InProgress,
    Completed,
    Failed,
    Cancelled,
    #[serde(other)]
    Unknown,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ToolCallLocation {
    #[serde(default)]
    pub file: Option<String>,
    #[serde(default)]
    pub line: Option<u64>,
    #[serde(flatten)]
    pub extra: Value,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AvailableCommand {
    #[serde(default)]
    pub id: String,
    #[serde(default)]
    pub name: String,
    #[serde(flatten)]
    pub extra: Value,
}

/// Inbound `session/request_permission`. The `json_rpc_id` is the wire id
/// that MUST be used verbatim in the reply (not OpenCode's internal permission id).
#[derive(Debug, Clone)]
pub struct PermissionRequest {
    pub json_rpc_id: i64,
    pub session_id: String,
    pub tool_call: PermissionToolCall,
    pub options: Vec<PermissionOption>,
    pub raw_params: Value,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PermissionToolCall {
    #[serde(default)]
    pub id: String,
    #[serde(default)]
    pub tool: String,
    #[serde(flatten)]
    pub extra: Value,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PermissionOption {
    #[serde(default)]
    pub option_id: String,
    #[serde(default)]
    pub kind: String,
    #[serde(default)]
    pub name: String,
}

#[derive(Debug, Clone)]
pub enum PermissionOutcome {
    Selected { option_id: String },
    Cancelled,
}

#[derive(Debug, Clone)]
pub struct PromptResult {
    pub stop_reason: StopReason,
    pub usage: Option<PromptUsage>,
    pub raw: Value,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum StopReason {
    EndTurn,
    MaxTokens,
    MaxTurnRequests,
    Refusal,
    Cancelled,
    Unknown(String),
}

impl StopReason {
    pub fn parse(s: &str) -> Self {
        match s {
            "end_turn" => Self::EndTurn,
            "max_tokens" => Self::MaxTokens,
            "max_turn_requests" => Self::MaxTurnRequests,
            "refusal" => Self::Refusal,
            "cancelled" => Self::Cancelled,
            other => Self::Unknown(other.to_string()),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PromptUsage {
    #[serde(default)]
    pub input_tokens: Option<u64>,
    #[serde(default)]
    pub output_tokens: Option<u64>,
    #[serde(flatten)]
    pub extra: Value,
}

fn extract_text_from_content(content: &Value) -> String {
    match content {
        Value::String(s) => s.clone(),
        Value::Object(obj) => obj
            .get("text")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .to_string(),
        Value::Array(arr) => arr
            .iter()
            .filter_map(|item| match item {
                Value::String(s) => Some(s.as_str()),
                Value::Object(obj) => obj.get("text").and_then(|v| v.as_str()),
                _ => None,
            })
            .collect::<Vec<_>>()
            .join(""),
        _ => String::new(),
    }
}

fn collapse_content_to_text(content: &Value) -> Option<String> {
    match content {
        Value::Array(arr) if !arr.is_empty() => {
            let mut parts = Vec::new();
            for item in arr {
                if let Some(t) = item.get("type").and_then(|v| v.as_str()) {
                    if t == "text" {
                        if let Some(text) = item.get("text").and_then(|v| v.as_str()) {
                            parts.push(text.to_string());
                        }
                    } else if t == "content" {
                        if let Some(inner) = item.get("content") {
                            if let Some(inner_type) = inner.get("type").and_then(|v| v.as_str()) {
                                if inner_type == "text" {
                                    if let Some(text) =
                                        inner.get("text").and_then(|v| v.as_str())
                                    {
                                        parts.push(text.to_string());
                                    }
                                }
                            }
                        }
                    }
                }
            }
            if parts.is_empty() {
                None
            } else {
                Some(parts.join(""))
            }
        }
        _ => None,
    }
}

pub fn parse_session_update(params: &Value) -> AcpUpdate {
    let update = match params.get("update") {
        Some(u) => u,
        None => {
            return AcpUpdate::Other {
                method: "session/update".to_string(),
                raw_params: params.clone(),
            }
        }
    };

    let discriminator = update
        .get("sessionUpdate")
        .and_then(|v| v.as_str())
        .unwrap_or("");

    let message_id = update
        .get("messageId")
        .or_else(|| update.get("message_id"))
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .to_string();

    match discriminator {
        "agent_message_chunk" => {
            let text = if let Some(content) = update.get("content") {
                extract_text_from_content(content)
            } else if let Some(t) = update.get("text").and_then(|v| v.as_str()) {
                t.to_string()
            } else {
                String::new()
            };
            AcpUpdate::AgentMessageChunk { message_id, text }
        }
        "agent_thought_chunk" => {
            let text = if let Some(content) = update.get("content") {
                extract_text_from_content(content)
            } else if let Some(t) = update.get("text").and_then(|v| v.as_str()) {
                t.to_string()
            } else {
                String::new()
            };
            AcpUpdate::AgentThoughtChunk { message_id, text }
        }
        "user_message_chunk" => {
            let text = if let Some(content) = update.get("content") {
                extract_text_from_content(content)
            } else if let Some(t) = update.get("text").and_then(|v| v.as_str()) {
                t.to_string()
            } else {
                String::new()
            };
            AcpUpdate::UserMessageChunk { message_id, text }
        }
        "plan" => {
            let entries: Vec<PlanEntry> = update
                .get("entries")
                .and_then(|v| serde_json::from_value(v.clone()).ok())
                .unwrap_or_default();
            AcpUpdate::Plan { entries }
        }
        "tool_call" => {
            let id = update
                .get("toolCallId")
                .or_else(|| update.get("id"))
                .and_then(|v| v.as_str())
                .unwrap_or("")
                .to_string();
            let title = update
                .get("title")
                .or_else(|| update.get("tool"))
                .and_then(|v| v.as_str())
                .unwrap_or("")
                .to_string();
            let kind: ToolKind = update
                .get("kind")
                .and_then(|v| serde_json::from_value(v.clone()).ok())
                .unwrap_or(ToolKind::Unknown);
            let status: ToolCallStatus = update
                .get("status")
                .and_then(|v| serde_json::from_value(v.clone()).ok())
                .unwrap_or(ToolCallStatus::Pending);
            let locations: Vec<ToolCallLocation> = update
                .get("locations")
                .and_then(|v| serde_json::from_value(v.clone()).ok())
                .unwrap_or_default();
            let raw_input = update.get("input").cloned().unwrap_or(Value::Null);
            AcpUpdate::ToolCall {
                id,
                title,
                kind,
                status,
                locations,
                raw_input,
            }
        }
        "tool_call_update" => {
            let id = update
                .get("toolCallId")
                .or_else(|| update.get("id"))
                .and_then(|v| v.as_str())
                .unwrap_or("")
                .to_string();
            let status: ToolCallStatus = update
                .get("status")
                .and_then(|v| serde_json::from_value(v.clone()).ok())
                .unwrap_or(ToolCallStatus::Unknown);
            let content = update.get("content");
            let content_text = content.and_then(collapse_content_to_text);
            let raw_output = content.cloned();
            AcpUpdate::ToolCallUpdate {
                id,
                status,
                content_text,
                raw_output,
            }
        }
        "available_commands_update" => {
            let commands: Vec<AvailableCommand> = update
                .get("commands")
                .and_then(|v| serde_json::from_value(v.clone()).ok())
                .unwrap_or_default();
            AcpUpdate::AvailableCommandsUpdate { commands }
        }
        "config_option_update" => {
            let options = update
                .get("options")
                .or_else(|| update.get("configOptions"))
                .cloned()
                .unwrap_or(Value::Null);
            AcpUpdate::ConfigOptionUpdate { options }
        }
        "usage_update" => {
            let used = update
                .get("used")
                .or_else(|| update.get("inputTokens"))
                .and_then(|v| v.as_u64());
            let size = update
                .get("size")
                .or_else(|| update.get("outputTokens"))
                .and_then(|v| v.as_u64());
            let cost: Option<UsageCost> = update
                .get("cost")
                .and_then(|v| serde_json::from_value(v.clone()).ok());
            AcpUpdate::UsageUpdate { used, size, cost }
        }
        _ => AcpUpdate::Other {
            method: discriminator.to_string(),
            raw_params: params.clone(),
        },
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn agent_message_chunk_content_object() {
        let params = json!({
            "update": {
                "sessionUpdate": "agent_message_chunk",
                "messageId": "msg1",
                "content": {"type": "text", "text": "hello"}
            }
        });
        match parse_session_update(&params) {
            AcpUpdate::AgentMessageChunk { message_id, text } => {
                assert_eq!(message_id, "msg1");
                assert_eq!(text, "hello");
            }
            other => panic!("expected AgentMessageChunk, got {:?}", other),
        }
    }

    #[test]
    fn agent_message_chunk_content_string() {
        let params = json!({
            "update": {
                "sessionUpdate": "agent_message_chunk",
                "content": "bare string"
            }
        });
        match parse_session_update(&params) {
            AcpUpdate::AgentMessageChunk { text, .. } => {
                assert_eq!(text, "bare string");
            }
            other => panic!("expected AgentMessageChunk, got {:?}", other),
        }
    }

    #[test]
    fn agent_message_chunk_text_field_fallback() {
        let params = json!({
            "update": {
                "sessionUpdate": "agent_message_chunk",
                "text": "fallback text"
            }
        });
        match parse_session_update(&params) {
            AcpUpdate::AgentMessageChunk { text, .. } => {
                assert_eq!(text, "fallback text");
            }
            other => panic!("expected AgentMessageChunk, got {:?}", other),
        }
    }

    #[test]
    fn usage_update_cost_object() {
        let params = json!({
            "update": {
                "sessionUpdate": "usage_update",
                "used": 100,
                "size": 50,
                "cost": {"amount": 0.005, "currency": "USD"}
            }
        });
        match parse_session_update(&params) {
            AcpUpdate::UsageUpdate { used, size, cost } => {
                assert_eq!(used, Some(100));
                assert_eq!(size, Some(50));
                let c = cost.unwrap();
                assert!((c.amount - 0.005).abs() < f64::EPSILON);
                assert_eq!(c.currency, "USD");
            }
            other => panic!("expected UsageUpdate, got {:?}", other),
        }
    }

    #[test]
    fn usage_update_missing_cost() {
        let params = json!({
            "update": {
                "sessionUpdate": "usage_update"
            }
        });
        match parse_session_update(&params) {
            AcpUpdate::UsageUpdate { used, size, cost } => {
                assert!(used.is_none());
                assert!(size.is_none());
                assert!(cost.is_none());
            }
            other => panic!("expected UsageUpdate, got {:?}", other),
        }
    }

    #[test]
    fn tool_call_update_content_collapse() {
        let params = json!({
            "update": {
                "sessionUpdate": "tool_call_update",
                "toolCallId": "tc1",
                "status": "completed",
                "content": [
                    {"type": "text", "text": "line1"},
                    {"type": "text", "text": "line2"}
                ]
            }
        });
        match parse_session_update(&params) {
            AcpUpdate::ToolCallUpdate {
                id,
                status,
                content_text,
                raw_output,
            } => {
                assert_eq!(id, "tc1");
                assert_eq!(status, ToolCallStatus::Completed);
                assert_eq!(content_text.as_deref(), Some("line1line2"));
                assert!(raw_output.is_some());
            }
            other => panic!("expected ToolCallUpdate, got {:?}", other),
        }
    }

    #[test]
    fn tool_call_update_all_statuses() {
        for (s, expected) in [
            ("pending", ToolCallStatus::Pending),
            ("in_progress", ToolCallStatus::InProgress),
            ("completed", ToolCallStatus::Completed),
            ("failed", ToolCallStatus::Failed),
            ("cancelled", ToolCallStatus::Cancelled),
        ] {
            let params = json!({
                "update": {
                    "sessionUpdate": "tool_call_update",
                    "toolCallId": "x",
                    "status": s
                }
            });
            match parse_session_update(&params) {
                AcpUpdate::ToolCallUpdate { status, .. } => {
                    assert_eq!(status, expected, "status mismatch for {}", s);
                }
                other => panic!("expected ToolCallUpdate, got {:?}", other),
            }
        }
    }

    #[test]
    fn unknown_discriminator_becomes_other() {
        let params = json!({
            "update": {
                "sessionUpdate": "future_event_type",
                "data": 42
            }
        });
        match parse_session_update(&params) {
            AcpUpdate::Other { method, .. } => {
                assert_eq!(method, "future_event_type");
            }
            other => panic!("expected Other, got {:?}", other),
        }
    }

    #[test]
    fn missing_update_field_becomes_other() {
        let params = json!({"something": "else"});
        match parse_session_update(&params) {
            AcpUpdate::Other { method, .. } => {
                assert_eq!(method, "session/update");
            }
            other => panic!("expected Other, got {:?}", other),
        }
    }

    #[test]
    fn tool_call_parse() {
        let params = json!({
            "update": {
                "sessionUpdate": "tool_call",
                "toolCallId": "tc99",
                "title": "Read file",
                "kind": "read",
                "status": "in_progress",
                "locations": [{"file": "foo.rs", "line": 10}],
                "input": {"path": "/tmp/foo"}
            }
        });
        match parse_session_update(&params) {
            AcpUpdate::ToolCall {
                id,
                title,
                kind,
                status,
                locations,
                raw_input,
            } => {
                assert_eq!(id, "tc99");
                assert_eq!(title, "Read file");
                assert_eq!(kind, ToolKind::Read);
                assert_eq!(status, ToolCallStatus::InProgress);
                assert_eq!(locations.len(), 1);
                assert_eq!(raw_input["path"], "/tmp/foo");
            }
            other => panic!("expected ToolCall, got {:?}", other),
        }
    }

    #[test]
    fn permission_outcome_serialization() {
        let _selected = PermissionOutcome::Selected {
            option_id: "once".to_string(),
        };
        let _cancelled = PermissionOutcome::Cancelled;
    }

    #[test]
    fn stop_reason_parse() {
        assert_eq!(StopReason::parse("end_turn"), StopReason::EndTurn);
        assert_eq!(StopReason::parse("cancelled"), StopReason::Cancelled);
        assert_eq!(StopReason::parse("max_tokens"), StopReason::MaxTokens);
        assert_eq!(
            StopReason::parse("something_new"),
            StopReason::Unknown("something_new".to_string())
        );
    }
}
