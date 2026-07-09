use maho_types::ai::AiMcpServer;
use maho_types::tool::ToolDescriptor;
use serde_json::Value;
use anyhow::{Result, anyhow};

const UNSUPPORTED: &str = "MCP client is not available on iOS (banned syscalls: fork/execve/setsid). Configure MCP servers on desktop.";

pub struct McpClient;

impl McpClient {
    pub async fn connect(_config: &AiMcpServer, _auth_token: Option<String>) -> Result<Self> {
        Err(anyhow!(UNSUPPORTED))
    }

    pub async fn list_tools(&self) -> Result<Vec<ToolDescriptor>> {
        Err(anyhow!(UNSUPPORTED))
    }

    pub async fn call_tool(&self, _name: &str, _args: Value) -> Result<Value> {
        Err(anyhow!(UNSUPPORTED))
    }

    pub async fn disconnect(&mut self) {}
}
