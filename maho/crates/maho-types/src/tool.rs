use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ToolDescriptor {
    pub name: String, // namespaced: "mcp:server/tool", "sh:name", bare for builtin
    pub description: String,
    #[serde(rename = "inputSchema")]
    pub parameters_schema: serde_json::Value, // JSON Schema
    pub provenance: ToolProvenance,
    pub sensitive: bool, // true = always prompt per-invocation
    pub permission: ToolPermission,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum ToolProvenance {
    BuiltinBrowser,
    AgentLocal,       // CLI tools, internal adapters
    External(String), // MCP server name
}

#[derive(Clone, Copy, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum ToolPermission {
    AutoApprove,
    SessionApprove,
    AlwaysAsk,
}

impl ToolDescriptor {
    pub fn to_openai_function(&self) -> serde_json::Value {
        let mut function = serde_json::Map::new();
        function.insert(
            "name".to_string(),
            serde_json::Value::String(self.name.clone()),
        );
        function.insert(
            "description".to_string(),
            serde_json::Value::String(self.description.clone()),
        );
        function.insert("parameters".to_string(), self.parameters_schema.clone());

        let mut root = serde_json::Map::new();
        root.insert(
            "type".to_string(),
            serde_json::Value::String("function".to_string()),
        );
        root.insert("function".to_string(), serde_json::Value::Object(function));
        serde_json::Value::Object(root)
    }
}
