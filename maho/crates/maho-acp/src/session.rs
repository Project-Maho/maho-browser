use std::path::PathBuf;

use serde::{Deserialize, Serialize};
use serde_json::Value;

#[derive(Debug, Clone, Serialize)]
pub struct McpServer {
    pub name: String,
    pub command: String,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub args: Vec<String>,
    #[serde(default, skip_serializing_if = "std::collections::HashMap::is_empty")]
    pub env: std::collections::HashMap<String, String>,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct NewSessionParams {
    pub cwd: PathBuf,
    pub mcp_servers: Vec<McpServer>,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct NewSessionResult {
    #[serde(alias = "id")]
    pub session_id: String,
    #[serde(default)]
    pub models: Value,
    #[serde(default)]
    pub modes: Value,
    #[serde(default)]
    pub config_options: Value,
    #[serde(flatten)]
    pub extra: Value,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct CloseSessionParams {
    pub session_id: String,
}
