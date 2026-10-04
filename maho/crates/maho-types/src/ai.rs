use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AiProfile {
    pub id: String,
    pub name: String,
    pub system_prompt: String,
    pub preferred_model: Option<String>,
    pub tools: Vec<String>,       // namespace refs
    pub mcp_servers: Vec<String>, // server IDs
    pub is_default: bool,
    pub created_at: String,
    pub updated_at: String,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AiWorkspace {
    pub id: String,
    pub name: String,
    pub profile_id: Option<String>,
    pub space_id: Option<String>,
    pub workspace_root: Option<String>,
    pub created_at: String,
    pub updated_at: String,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AiMcpServer {
    pub id: String,
    pub workspace_id: String,
    pub name: String,
    pub transport: McpTransport,
    pub command: Option<String>,
    pub url: Option<String>,
    pub auth_keychain_id: Option<String>,
    pub trusted: bool,
    pub trusted_tools: Option<Vec<String>>,
    pub timeout_ms: u64,
    pub output_cap_bytes: usize,
    pub socket_path: Option<String>,
    pub created_at: String,
    pub updated_at: String,
}

#[derive(Clone, Copy, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum McpTransport {
    Stdio,
    Http,
    Uds,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AiCliTool {
    pub id: String,
    pub workspace_id: String,
    pub name: String,
    pub description: String,
    pub command_template: String,
    pub parameters_schema: serde_json::Value,
    pub sensitive: bool,
    pub timeout_ms: u64,
    pub output_cap_bytes: usize,
    pub working_directory: Option<String>,
    pub created_at: String,
    pub updated_at: String,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SessionConfig {
    pub system_prompt: Option<String>,
    pub model: Option<String>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ResolvedConfig {
    pub system_prompt: String,
    pub model: String,
}
