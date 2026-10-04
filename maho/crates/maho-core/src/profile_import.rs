use maho_storage::sqlite::SqliteStorage;
use maho_types::ai::{AiCliTool, AiMcpServer, AiProfile, McpTransport};
use serde::Deserialize;

#[derive(Deserialize, Debug)]
struct TomlProfileImport {
    profile: TomlProfile,
    tools: Option<TomlTools>,
}

#[derive(Deserialize, Debug)]
struct TomlProfile {
    name: String,
    system_prompt: String,
    preferred_model: Option<String>,
}

#[derive(Deserialize, Debug)]
struct TomlTools {
    mcp: Option<Vec<TomlMcpServer>>,
    cli: Option<Vec<TomlCliTool>>,
}

#[derive(Deserialize, Debug)]
struct TomlMcpServer {
    name: String,
    transport: String,
    command: Option<String>,
    url: Option<String>,
    auth_keychain_id: Option<String>,
    socket_path: Option<String>,
    // SECURITY: `trusted` is intentionally parsed but IGNORED on import.
    // Imports MUST NOT auto-trust servers; user must approve via the
    // trust-ceremony UI at first connect. Preserved in struct only for
    // backward-compat with existing TOML files.
    #[allow(dead_code)]
    trusted: Option<bool>,
    timeout_ms: Option<u64>,
    output_cap_bytes: Option<usize>,
}

#[derive(Deserialize, Debug)]
struct TomlCliTool {
    name: String,
    description: String,
    command_template: String,
    parameters_schema: String, // JSON Schema string
    // SECURITY: `sensitive` is intentionally parsed but IGNORED on import.
    // Imports MUST force sensitive = true for CLI tools.
    // Preserved in struct only for backward-compat with existing TOML files.
    #[allow(dead_code)]
    sensitive: Option<bool>,
    timeout_ms: Option<u64>,
    output_cap_bytes: Option<usize>,
    working_directory: Option<String>,
}

pub fn import_profile_from_toml(
    storage: &SqliteStorage,
    workspace_id: &str,
    toml_str: &str,
) -> Result<String, String> {
    // 1. Verify workspace exists
    let workspaces = storage.list_workspaces().map_err(|e| e.to_string())?;
    let ws = workspaces
        .into_iter()
        .find(|w| w.id == workspace_id)
        .ok_or_else(|| format!("Workspace '{}' not found", workspace_id))?;

    // 2. Parse TOML
    let import_data: TomlProfileImport =
        toml::from_str(toml_str).map_err(|e| format!("Invalid TOML schema: {}", e))?;

    if import_data.profile.name.trim().is_empty() {
        return Err("Profile name cannot be empty".to_string());
    }

    let now = maho_types::common::DateTime::now().0;
    let profile_id = uuid::Uuid::new_v4().to_string();

    let mut profile_tools = Vec::new();
    let mut profile_mcp_servers = Vec::new();

    // 3. Process MCP Servers
    if let Some(ref tools) = import_data.tools {
        if let Some(ref mcps) = tools.mcp {
            for mcp in mcps {
                if mcp.name.trim().is_empty() {
                    return Err("MCP server name cannot be empty".to_string());
                }
                let transport = match mcp.transport.as_str() {
                    "stdio" => McpTransport::Stdio,
                    "http" => McpTransport::Http,
                    "uds" => McpTransport::Uds,
                    other => return Err(format!("Unsupported MCP transport: {}", other)),
                };
                if transport == McpTransport::Stdio && mcp.command.is_none() {
                    return Err(format!(
                        "Command is required for stdio MCP server '{}'",
                        mcp.name
                    ));
                }
                if transport == McpTransport::Http && mcp.url.is_none() {
                    return Err(format!(
                        "URL is required for http MCP server '{}'",
                        mcp.name
                    ));
                }
                if transport == McpTransport::Uds && mcp.socket_path.is_none() {
                    return Err(format!(
                        "Socket path is required for UDS MCP server '{}'",
                        mcp.name
                    ));
                }

                let server_id = uuid::Uuid::new_v4().to_string();
                let server = AiMcpServer {
                    id: server_id.clone(),
                    workspace_id: workspace_id.to_string(),
                    name: mcp.name.clone(),
                    transport,
                    command: mcp.command.clone(),
                    url: mcp.url.clone(),
                    auth_keychain_id: mcp.auth_keychain_id.clone(),
                    trusted: false,
                    trusted_tools: None,
                    timeout_ms: mcp.timeout_ms.unwrap_or(60000).min(60_000),
                    output_cap_bytes: mcp.output_cap_bytes.unwrap_or(10240).min(1024 * 1024),
                    created_at: now.clone(),
                    updated_at: now.clone(),
                    socket_path: mcp.socket_path.clone(),
                };

                storage
                    .create_mcp_server(&server)
                    .map_err(|e| e.to_string())?;
                profile_mcp_servers.push(server_id);
            }
        }

        if let Some(ref clis) = tools.cli {
            for cli in clis {
                if cli.name.trim().is_empty() {
                    return Err("CLI tool name cannot be empty".to_string());
                }
                let parameters_schema: serde_json::Value =
                    serde_json::from_str(&cli.parameters_schema).map_err(|e| {
                        format!("Invalid JSON schema in CLI tool '{}': {}", cli.name, e)
                    })?;

                let tool_id = uuid::Uuid::new_v4().to_string();
                let tool = AiCliTool {
                    id: tool_id,
                    workspace_id: workspace_id.to_string(),
                    name: cli.name.clone(),
                    description: cli.description.clone(),
                    command_template: cli.command_template.clone(),
                    parameters_schema,
                    sensitive: true,
                    timeout_ms: cli.timeout_ms.unwrap_or(60000).min(60_000),
                    output_cap_bytes: cli.output_cap_bytes.unwrap_or(10240).min(1024 * 1024),
                    working_directory: cli.working_directory.clone(),
                    created_at: now.clone(),
                    updated_at: now.clone(),
                };

                storage.create_cli_tool(&tool).map_err(|e| e.to_string())?;
                profile_tools.push(format!("sh:{}", cli.name));
            }
        }
    }

    // 5. Create Profile
    let profile = AiProfile {
        id: profile_id.clone(),
        name: import_data.profile.name,
        system_prompt: import_data.profile.system_prompt,
        preferred_model: import_data.profile.preferred_model,
        tools: profile_tools,
        mcp_servers: profile_mcp_servers,
        is_default: false,
        created_at: now.clone(),
        updated_at: now.clone(),
    };

    storage
        .create_ai_profile(&profile)
        .map_err(|e| e.to_string())?;

    // 6. Update workspace to use this profile
    let mut updated_ws = ws;
    updated_ws.profile_id = Some(profile_id.clone());
    updated_ws.updated_at = now;
    storage
        .update_workspace(&updated_ws)
        .map_err(|e| e.to_string())?;

    Ok(profile_id)
}
