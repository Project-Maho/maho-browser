use serde::{Deserialize, Serialize};

pub use crate::tool::{ToolDescriptor, ToolPermission, ToolProvenance};

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ToolCall {
    pub id: String,
    pub name: String,
    pub arguments: serde_json::Value,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ToolResult {
    pub call_id: String,
    pub success: bool,
    pub content: String,
}

pub type ToolDefinition = ToolDescriptor;
