use crate::error::StorageError;
use crate::sqlite::SqliteStorage;
use maho_types::ai::{AiCliTool, AiMcpServer, AiProfile, AiWorkspace, McpTransport};
use rusqlite::params;

impl SqliteStorage {
    // === AI Profiles ===

    pub fn create_ai_profile(&self, profile: &AiProfile) -> Result<(), StorageError> {
        let tools_json = serde_json::to_string(&profile.tools).unwrap_or_else(|_| "[]".to_string());
        let mcp_servers_json =
            serde_json::to_string(&profile.mcp_servers).unwrap_or_else(|_| "[]".to_string());
        let is_default_val = if profile.is_default { 1 } else { 0 };

        self.conn.execute(
            "INSERT INTO ai_profiles (id, name, system_prompt, preferred_model, tools_json, mcp_servers_json, is_default, created_at, updated_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)",
            params![
                profile.id,
                profile.name,
                profile.system_prompt,
                profile.preferred_model,
                tools_json,
                mcp_servers_json,
                is_default_val,
                profile.created_at,
                profile.updated_at,
            ],
        )?;

        Ok(())
    }

    pub fn get_ai_profile(&self, id: &str) -> Result<Option<AiProfile>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, name, system_prompt, preferred_model, tools_json, mcp_servers_json, is_default, created_at, updated_at
             FROM ai_profiles WHERE id = ?1"
        )?;

        let mut rows = stmt.query(params![id])?;

        if let Some(row) = rows.next()? {
            let tools_json: String = row.get(4)?;
            let mcp_servers_json: String = row.get(5)?;
            let is_default_val: i32 = row.get(6)?;

            let tools: Vec<String> = serde_json::from_str(&tools_json).unwrap_or_default();
            let mcp_servers: Vec<String> =
                serde_json::from_str(&mcp_servers_json).unwrap_or_default();

            Ok(Some(AiProfile {
                id: row.get(0)?,
                name: row.get(1)?,
                system_prompt: row.get(2)?,
                preferred_model: row.get(3)?,
                tools,
                mcp_servers,
                is_default: is_default_val != 0,
                created_at: row.get(7)?,
                updated_at: row.get(8)?,
            }))
        } else {
            Ok(None)
        }
    }

    pub fn list_ai_profiles(&self) -> Result<Vec<AiProfile>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, name, system_prompt, preferred_model, tools_json, mcp_servers_json, is_default, created_at, updated_at
             FROM ai_profiles"
        )?;

        let rows = stmt.query_map(params![], |row| {
            let tools_json: String = row.get(4)?;
            let mcp_servers_json: String = row.get(5)?;
            let is_default_val: i32 = row.get(6)?;

            let tools: Vec<String> = serde_json::from_str(&tools_json).unwrap_or_default();
            let mcp_servers: Vec<String> =
                serde_json::from_str(&mcp_servers_json).unwrap_or_default();

            Ok(AiProfile {
                id: row.get(0)?,
                name: row.get(1)?,
                system_prompt: row.get(2)?,
                preferred_model: row.get(3)?,
                tools,
                mcp_servers,
                is_default: is_default_val != 0,
                created_at: row.get(7)?,
                updated_at: row.get(8)?,
            })
        })?;

        let mut res = Vec::new();
        for r in rows {
            res.push(r?);
        }
        Ok(res)
    }

    pub fn update_ai_profile(&self, profile: &AiProfile) -> Result<(), StorageError> {
        let tools_json = serde_json::to_string(&profile.tools).unwrap_or_else(|_| "[]".to_string());
        let mcp_servers_json =
            serde_json::to_string(&profile.mcp_servers).unwrap_or_else(|_| "[]".to_string());
        let is_default_val = if profile.is_default { 1 } else { 0 };

        self.conn.execute(
            "UPDATE ai_profiles
             SET name = ?2, system_prompt = ?3, preferred_model = ?4, tools_json = ?5, mcp_servers_json = ?6, is_default = ?7, updated_at = ?8
             WHERE id = ?1",
            params![
                profile.id,
                profile.name,
                profile.system_prompt,
                profile.preferred_model,
                tools_json,
                mcp_servers_json,
                is_default_val,
                profile.updated_at,
            ],
        )?;

        Ok(())
    }

    pub fn delete_ai_profile(&self, id: &str) -> Result<(), StorageError> {
        self.conn
            .execute("DELETE FROM ai_profiles WHERE id = ?1", params![id])?;
        Ok(())
    }

    pub fn get_default_ai_profile(&self) -> Result<Option<AiProfile>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, name, system_prompt, preferred_model, tools_json, mcp_servers_json, is_default, created_at, updated_at
             FROM ai_profiles WHERE is_default = 1 LIMIT 1"
        )?;

        let mut rows = stmt.query(params![])?;

        if let Some(row) = rows.next()? {
            let tools_json: String = row.get(4)?;
            let mcp_servers_json: String = row.get(5)?;
            let is_default_val: i32 = row.get(6)?;

            let tools: Vec<String> = serde_json::from_str(&tools_json).unwrap_or_default();
            let mcp_servers: Vec<String> =
                serde_json::from_str(&mcp_servers_json).unwrap_or_default();

            Ok(Some(AiProfile {
                id: row.get(0)?,
                name: row.get(1)?,
                system_prompt: row.get(2)?,
                preferred_model: row.get(3)?,
                tools,
                mcp_servers,
                is_default: is_default_val != 0,
                created_at: row.get(7)?,
                updated_at: row.get(8)?,
            }))
        } else {
            Ok(None)
        }
    }

    // === Workspaces ===

    pub fn create_workspace(&self, ws: &AiWorkspace) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO ai_workspaces (id, name, profile_id, space_id, workspace_root, created_at, updated_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
            params![
                ws.id,
                ws.name,
                ws.profile_id,
                ws.space_id,
                ws.workspace_root,
                ws.created_at,
                ws.updated_at,
            ],
        )?;
        Ok(())
    }

    pub fn get_workspace(&self, id: &str) -> Result<Option<AiWorkspace>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, name, profile_id, space_id, workspace_root, created_at, updated_at
             FROM ai_workspaces WHERE id = ?1",
        )?;

        let mut rows = stmt.query(params![id])?;

        if let Some(row) = rows.next()? {
            Ok(Some(AiWorkspace {
                id: row.get(0)?,
                name: row.get(1)?,
                profile_id: row.get(2)?,
                space_id: row.get(3)?,
                workspace_root: row.get(4)?,
                created_at: row.get(5)?,
                updated_at: row.get(6)?,
            }))
        } else {
            Ok(None)
        }
    }

    pub fn get_workspace_by_space(
        &self,
        space_id: &str,
    ) -> Result<Option<AiWorkspace>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, name, profile_id, space_id, workspace_root, created_at, updated_at
             FROM ai_workspaces WHERE space_id = ?1",
        )?;

        let mut rows = stmt.query(params![space_id])?;

        if let Some(row) = rows.next()? {
            Ok(Some(AiWorkspace {
                id: row.get(0)?,
                name: row.get(1)?,
                profile_id: row.get(2)?,
                space_id: row.get(3)?,
                workspace_root: row.get(4)?,
                created_at: row.get(5)?,
                updated_at: row.get(6)?,
            }))
        } else {
            Ok(None)
        }
    }

    pub fn list_workspaces(&self) -> Result<Vec<AiWorkspace>, StorageError> {
        // ORDER BY created_at, id ensures deterministic output: both CLI and WebUI call this
        // query and use workspaces[0] as the default; without ordering the first row is implicit
        // rowid order which is not guaranteed stable across vacuums or migrations.
        let mut stmt = self.conn.prepare(
            "SELECT id, name, profile_id, space_id, workspace_root, created_at, updated_at
             FROM ai_workspaces
             ORDER BY created_at, id",
        )?;

        let rows = stmt.query_map(params![], |row| {
            Ok(AiWorkspace {
                id: row.get(0)?,
                name: row.get(1)?,
                profile_id: row.get(2)?,
                space_id: row.get(3)?,
                workspace_root: row.get(4)?,
                created_at: row.get(5)?,
                updated_at: row.get(6)?,
            })
        })?;

        let mut res = Vec::new();
        for r in rows {
            res.push(r?);
        }
        Ok(res)
    }

    pub fn update_workspace(&self, ws: &AiWorkspace) -> Result<(), StorageError> {
        self.conn.execute(
            "UPDATE ai_workspaces
             SET name = ?2, profile_id = ?3, space_id = ?4, workspace_root = ?5, updated_at = ?6
             WHERE id = ?1",
            params![
                ws.id,
                ws.name,
                ws.profile_id,
                ws.space_id,
                ws.workspace_root,
                ws.updated_at,
            ],
        )?;
        Ok(())
    }

    pub fn delete_workspace(&self, id: &str) -> Result<(), StorageError> {
        self.conn
            .execute("DELETE FROM ai_workspaces WHERE id = ?1", params![id])?;
        Ok(())
    }

    // === MCP Servers ===

    pub fn create_mcp_server(&self, server: &AiMcpServer) -> Result<(), StorageError> {
        let transport_str = match server.transport {
            McpTransport::Stdio => "stdio",
            McpTransport::Http => "http",
            McpTransport::Uds => "uds",
        };
        let trusted_val = if server.trusted { 1 } else { 0 };
        let trusted_tools_json = server
            .trusted_tools
            .as_ref()
            .map(|t| serde_json::to_string(t).unwrap_or_default());

        self.conn.execute(
            "INSERT INTO ai_mcp_servers (id, workspace_id, name, transport, command, url, auth_keychain_id, trusted, trusted_tools_json, timeout_ms, output_cap_bytes, created_at, updated_at, socket_path)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14)",
            params![
                server.id,
                server.workspace_id,
                server.name,
                transport_str,
                server.command,
                server.url,
                server.auth_keychain_id,
                trusted_val,
                trusted_tools_json,
                server.timeout_ms as i64,
                server.output_cap_bytes as i64,
                server.created_at,
                server.updated_at,
                server.socket_path,
            ],
        )?;
        Ok(())
    }

    pub fn get_mcp_server(&self, id: &str) -> Result<Option<AiMcpServer>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, workspace_id, name, transport, command, url, auth_keychain_id, trusted, trusted_tools_json, timeout_ms, output_cap_bytes, created_at, updated_at, socket_path
             FROM ai_mcp_servers WHERE id = ?1"
        )?;

        let mut rows = stmt.query(params![id])?;

        if let Some(row) = rows.next()? {
            let transport_str: String = row.get(3)?;
            let transport = match transport_str.as_str() {
                "http" => McpTransport::Http,
                "uds" => McpTransport::Uds,
                _ => McpTransport::Stdio,
            };
            let trusted_val: i32 = row.get(7)?;
            let trusted_tools_str: Option<String> = row.get(8)?;
            let trusted_tools = trusted_tools_str.and_then(|s| serde_json::from_str(&s).ok());
            let timeout_ms: i64 = row.get(9)?;
            let output_cap_bytes: i64 = row.get(10)?;

            Ok(Some(AiMcpServer {
                id: row.get(0)?,
                workspace_id: row.get(1)?,
                name: row.get(2)?,
                transport,
                command: row.get(4)?,
                url: row.get(5)?,
                auth_keychain_id: row.get(6)?,
                trusted: trusted_val != 0,
                trusted_tools,
                timeout_ms: timeout_ms as u64,
                output_cap_bytes: output_cap_bytes as usize,
                created_at: row.get(11)?,
                updated_at: row.get(12)?,
                socket_path: row.get(13)?,
            }))
        } else {
            Ok(None)
        }
    }

    pub fn list_mcp_servers(&self, workspace_id: &str) -> Result<Vec<AiMcpServer>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, workspace_id, name, transport, command, url, auth_keychain_id, trusted, trusted_tools_json, timeout_ms, output_cap_bytes, created_at, updated_at, socket_path
             FROM ai_mcp_servers WHERE workspace_id = ?1"
        )?;

        let rows = stmt.query_map(params![workspace_id], |row| {
            let transport_str: String = row.get(3)?;
            let transport = match transport_str.as_str() {
                "http" => McpTransport::Http,
                "uds" => McpTransport::Uds,
                _ => McpTransport::Stdio,
            };
            let trusted_val: i32 = row.get(7)?;
            let trusted_tools_str: Option<String> = row.get(8)?;
            let trusted_tools = trusted_tools_str.and_then(|s| serde_json::from_str(&s).ok());
            let timeout_ms: i64 = row.get(9)?;
            let output_cap_bytes: i64 = row.get(10)?;

            Ok(AiMcpServer {
                id: row.get(0)?,
                workspace_id: row.get(1)?,
                name: row.get(2)?,
                transport,
                command: row.get(4)?,
                url: row.get(5)?,
                auth_keychain_id: row.get(6)?,
                trusted: trusted_val != 0,
                trusted_tools,
                timeout_ms: timeout_ms as u64,
                output_cap_bytes: output_cap_bytes as usize,
                created_at: row.get(11)?,
                updated_at: row.get(12)?,
                socket_path: row.get(13)?,
            })
        })?;

        let mut res = Vec::new();
        for r in rows {
            res.push(r?);
        }
        Ok(res)
    }

    pub fn update_mcp_server(&self, server: &AiMcpServer) -> Result<(), StorageError> {
        let transport_str = match server.transport {
            McpTransport::Stdio => "stdio",
            McpTransport::Http => "http",
            McpTransport::Uds => "uds",
        };
        let trusted_val = if server.trusted { 1 } else { 0 };
        let trusted_tools_json = server
            .trusted_tools
            .as_ref()
            .map(|t| serde_json::to_string(t).unwrap_or_default());

        self.conn.execute(
            "UPDATE ai_mcp_servers
             SET name = ?3, transport = ?4, command = ?5, url = ?6, auth_keychain_id = ?7, trusted = ?8, trusted_tools_json = ?9, timeout_ms = ?10, output_cap_bytes = ?11, updated_at = ?12, socket_path = ?13
             WHERE id = ?1 AND workspace_id = ?2",
            params![
                server.id,
                server.workspace_id,
                server.name,
                transport_str,
                server.command,
                server.url,
                server.auth_keychain_id,
                trusted_val,
                trusted_tools_json,
                server.timeout_ms as i64,
                server.output_cap_bytes as i64,
                server.updated_at,
                server.socket_path,
            ],
        )?;
        Ok(())
    }

    pub fn delete_mcp_server(&self, workspace_id: &str, name: &str) -> Result<(), StorageError> {
        self.conn.execute(
            "DELETE FROM ai_mcp_servers WHERE workspace_id = ?1 AND name = ?2",
            params![workspace_id, name],
        )?;
        Ok(())
    }

    // === CLI Tools ===

    pub fn create_cli_tool(&self, tool: &AiCliTool) -> Result<(), StorageError> {
        let sensitive_val = if tool.sensitive { 1 } else { 0 };
        let schema_str =
            serde_json::to_string(&tool.parameters_schema).unwrap_or_else(|_| "{}".to_string());

        self.conn.execute(
            "INSERT INTO ai_cli_tools (id, workspace_id, name, description, command_template, parameters_schema_json, sensitive, timeout_ms, output_cap_bytes, working_directory, created_at, updated_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)",
            params![
                tool.id,
                tool.workspace_id,
                tool.name,
                tool.description,
                tool.command_template,
                schema_str,
                sensitive_val,
                tool.timeout_ms as i64,
                tool.output_cap_bytes as i64,
                tool.working_directory,
                tool.created_at,
                tool.updated_at,
            ],
        )?;
        Ok(())
    }

    pub fn get_cli_tool(&self, id: &str) -> Result<Option<AiCliTool>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, workspace_id, name, description, command_template, parameters_schema_json, sensitive, timeout_ms, output_cap_bytes, working_directory, created_at, updated_at
             FROM ai_cli_tools WHERE id = ?1"
        )?;

        let mut rows = stmt.query(params![id])?;

        if let Some(row) = rows.next()? {
            let schema_str: String = row.get(5)?;
            let parameters_schema: serde_json::Value =
                serde_json::from_str(&schema_str).unwrap_or_default();
            let sensitive_val: i32 = row.get(6)?;
            let timeout_ms: i64 = row.get(7)?;
            let output_cap_bytes: i64 = row.get(8)?;

            Ok(Some(AiCliTool {
                id: row.get(0)?,
                workspace_id: row.get(1)?,
                name: row.get(2)?,
                description: row.get(3)?,
                command_template: row.get(4)?,
                parameters_schema,
                sensitive: sensitive_val != 0,
                timeout_ms: timeout_ms as u64,
                output_cap_bytes: output_cap_bytes as usize,
                working_directory: row.get(9)?,
                created_at: row.get(10)?,
                updated_at: row.get(11)?,
            }))
        } else {
            Ok(None)
        }
    }

    pub fn list_cli_tools(&self, workspace_id: &str) -> Result<Vec<AiCliTool>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT id, workspace_id, name, description, command_template, parameters_schema_json, sensitive, timeout_ms, output_cap_bytes, working_directory, created_at, updated_at
             FROM ai_cli_tools WHERE workspace_id = ?1"
        )?;

        let rows = stmt.query_map(params![workspace_id], |row| {
            let schema_str: String = row.get(5)?;
            let parameters_schema: serde_json::Value =
                serde_json::from_str(&schema_str).unwrap_or_default();
            let sensitive_val: i32 = row.get(6)?;
            let timeout_ms: i64 = row.get(7)?;
            let output_cap_bytes: i64 = row.get(8)?;

            Ok(AiCliTool {
                id: row.get(0)?,
                workspace_id: row.get(1)?,
                name: row.get(2)?,
                description: row.get(3)?,
                command_template: row.get(4)?,
                parameters_schema,
                sensitive: sensitive_val != 0,
                timeout_ms: timeout_ms as u64,
                output_cap_bytes: output_cap_bytes as usize,
                working_directory: row.get(9)?,
                created_at: row.get(10)?,
                updated_at: row.get(11)?,
            })
        })?;

        let mut res = Vec::new();
        for r in rows {
            res.push(r?);
        }
        Ok(res)
    }

    pub fn update_cli_tool(&self, tool: &AiCliTool) -> Result<(), StorageError> {
        let sensitive_val = if tool.sensitive { 1 } else { 0 };
        let schema_str =
            serde_json::to_string(&tool.parameters_schema).unwrap_or_else(|_| "{}".to_string());

        self.conn.execute(
            "UPDATE ai_cli_tools
             SET name = ?3, description = ?4, command_template = ?5, parameters_schema_json = ?6, sensitive = ?7, timeout_ms = ?8, output_cap_bytes = ?9, working_directory = ?10, updated_at = ?11
             WHERE id = ?1 AND workspace_id = ?2",
            params![
                tool.id,
                tool.workspace_id,
                tool.name,
                tool.description,
                tool.command_template,
                schema_str,
                sensitive_val,
                tool.timeout_ms as i64,
                tool.output_cap_bytes as i64,
                tool.working_directory,
                tool.updated_at,
            ],
        )?;
        Ok(())
    }

    pub fn delete_cli_tool(&self, workspace_id: &str, name: &str) -> Result<(), StorageError> {
        self.conn.execute(
            "DELETE FROM ai_cli_tools WHERE workspace_id = ?1 AND name = ?2",
            params![workspace_id, name],
        )?;
        Ok(())
    }
}
