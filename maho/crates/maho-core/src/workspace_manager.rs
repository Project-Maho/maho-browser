use crate::tool_registry::ToolRegistry;
use maho_storage::sqlite::SqliteStorage;
use maho_types::ai::{AiCliTool, AiMcpServer, AiWorkspace};
use maho_types::tool::{ToolDescriptor, ToolPermission, ToolProvenance};

pub struct WorkspaceManager {
    pub storage: SqliteStorage,
}

impl WorkspaceManager {
    pub fn new(storage: SqliteStorage) -> Self {
        Self { storage }
    }

    pub fn create_workspace(
        &self,
        name: &str,
        space_id: Option<&str>,
    ) -> Result<AiWorkspace, String> {
        let id = uuid::Uuid::new_v4().to_string();
        let now = chrono::Utc::now().to_rfc3339();
        let ws = AiWorkspace {
            id,
            name: name.to_string(),
            profile_id: Some("blank".to_string()),
            space_id: space_id.map(String::from),
            workspace_root: None,
            created_at: now.clone(),
            updated_at: now,
        };
        self.storage
            .create_workspace(&ws)
            .map_err(|e| e.to_string())?;
        Ok(ws)
    }

    pub fn get_active_workspace(&self, space_id: &str) -> Result<AiWorkspace, String> {
        if let Some(ws) = self
            .storage
            .get_workspace_by_space(space_id)
            .map_err(|e| e.to_string())?
        {
            Ok(ws)
        } else {
            self.create_workspace("Default Workspace", Some(space_id))
        }
    }

    pub fn switch_profile(&self, workspace_id: &str, profile_id: &str) -> Result<(), String> {
        let mut ws = self
            .storage
            .get_workspace(workspace_id)
            .map_err(|e| e.to_string())?
            .ok_or_else(|| format!("Workspace not found: {}", workspace_id))?;
        ws.profile_id = Some(profile_id.to_string());
        ws.updated_at = chrono::Utc::now().to_rfc3339();
        self.storage
            .update_workspace(&ws)
            .map_err(|e| e.to_string())
    }

    pub fn register_mcp_server(
        &self,
        workspace_id: &str,
        mut config: AiMcpServer,
    ) -> Result<(), String> {
        config.workspace_id = workspace_id.to_string();
        config.updated_at = chrono::Utc::now().to_rfc3339();
        let exists = self
            .storage
            .get_mcp_server(&config.id)
            .map_err(|e| e.to_string())?
            .is_some();
        if exists {
            self.storage
                .update_mcp_server(&config)
                .map_err(|e| e.to_string())
        } else {
            self.storage
                .create_mcp_server(&config)
                .map_err(|e| e.to_string())
        }
    }

    pub fn register_cli_tool(&self, workspace_id: &str, mut tool: AiCliTool) -> Result<(), String> {
        tool.workspace_id = workspace_id.to_string();
        tool.updated_at = chrono::Utc::now().to_rfc3339();
        let exists = self
            .storage
            .get_cli_tool(&tool.id)
            .map_err(|e| e.to_string())?
            .is_some();
        if exists {
            self.storage
                .update_cli_tool(&tool)
                .map_err(|e| e.to_string())
        } else {
            self.storage
                .create_cli_tool(&tool)
                .map_err(|e| e.to_string())
        }
    }

    pub fn remove_mcp_server(&self, workspace_id: &str, name: &str) -> Result<(), String> {
        self.storage
            .delete_mcp_server(workspace_id, name)
            .map_err(|e| e.to_string())
    }

    pub fn remove_cli_tool(&self, workspace_id: &str, name: &str) -> Result<(), String> {
        self.storage
            .delete_cli_tool(workspace_id, name)
            .map_err(|e| e.to_string())
    }

    pub fn get_effective_tools(
        &self,
        workspace_id: &str,
        registry: &ToolRegistry,
    ) -> Result<Vec<ToolDescriptor>, String> {
        let ws = self
            .storage
            .get_workspace(workspace_id)
            .map_err(|e| e.to_string())?
            .ok_or_else(|| format!("Workspace not found: {}", workspace_id))?;

        let profile_id = ws.profile_id.unwrap_or_else(|| "blank".to_string());
        let profile = self
            .storage
            .get_ai_profile(&profile_id)
            .map_err(|e| e.to_string())?
            .ok_or_else(|| format!("Profile not found: {}", profile_id))?;

        let mut candidates = Vec::new();
        for name in registry.list_tool_names() {
            if let Some(tool) = registry.get_tool(&name) {
                candidates.push(tool.clone());
            }
        }

        let cli_tools = self
            .storage
            .list_cli_tools(workspace_id)
            .map_err(|e| e.to_string())?;
        for tool in cli_tools {
            candidates.push(ToolDescriptor {
                name: format!("sh:{}", tool.name),
                description: tool.description.clone(),
                parameters_schema: tool.parameters_schema.clone(),
                provenance: ToolProvenance::AgentLocal,
                sensitive: tool.sensitive,
                permission: if tool.sensitive {
                    ToolPermission::AlwaysAsk
                } else {
                    ToolPermission::AutoApprove
                },
            });
        }

        if profile.tools.is_empty() {
            Ok(candidates)
        } else {
            let filtered = candidates
                .into_iter()
                .filter(|t| profile.tools.contains(&t.name))
                .collect();
            Ok(filtered)
        }
    }
}
