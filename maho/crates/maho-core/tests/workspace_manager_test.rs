use maho_core::tool_registry::ToolRegistry;
use maho_core::workspace_manager::WorkspaceManager;
use maho_storage::sqlite::SqliteStorage;
use maho_types::ai::{AiCliTool, AiMcpServer, McpTransport};
use maho_types::tool::{ToolDescriptor, ToolPermission, ToolProvenance};

#[test]
fn test_create_workspace_assigns_blank_profile() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let storage = SqliteStorage::open_in_memory().unwrap();
    let manager = WorkspaceManager::new(storage);

    let ws = manager
        .create_workspace("Test WS", Some("space-1"))
        .unwrap();
    assert_eq!(ws.name, "Test WS");
    assert_eq!(ws.profile_id, Some("blank".to_string()));
    assert_eq!(ws.space_id, Some("space-1".to_string()));
}

#[test]
fn test_switch_profile_updates_workspace() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let storage = SqliteStorage::open_in_memory().unwrap();
    let manager = WorkspaceManager::new(storage);

    let ws = manager.create_workspace("Test WS", None).unwrap();
    assert_eq!(ws.profile_id, Some("blank".to_string()));

    manager.switch_profile(&ws.id, "code_reviewer").unwrap();

    let ws_updated = manager.storage.get_workspace(&ws.id).unwrap().unwrap();
    assert_eq!(ws_updated.profile_id, Some("code_reviewer".to_string()));
}

#[test]
fn test_get_active_workspace_creates_if_missing() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let storage = SqliteStorage::open_in_memory().unwrap();
    let manager = WorkspaceManager::new(storage);

    let ws = manager.get_active_workspace("space-1").unwrap();
    assert_eq!(ws.space_id, Some("space-1".to_string()));
    assert_eq!(ws.profile_id, Some("blank".to_string()));

    let ws2 = manager.get_active_workspace("space-1").unwrap();
    assert_eq!(ws.id, ws2.id);
}

#[test]
fn test_register_mcp_server_and_tools() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let storage = SqliteStorage::open_in_memory().unwrap();
    let manager = WorkspaceManager::new(storage);

    let ws = manager.create_workspace("Test WS", None).unwrap();

    let mcp = AiMcpServer {
        id: "mcp-1".to_string(),
        workspace_id: ws.id.clone(),
        name: "test-mcp".to_string(),
        transport: McpTransport::Stdio,
        command: Some("npx echo".to_string()),
        url: None,
        auth_keychain_id: None,
        trusted: true,
        trusted_tools: Some(vec!["mcp:test-mcp/test_tool".to_string()]),
        timeout_ms: 1000,
        output_cap_bytes: 1024,
        socket_path: None,
        created_at: "".to_string(),
        updated_at: "".to_string(),
    };

    manager.register_mcp_server(&ws.id, mcp).unwrap();

    let mut registry = ToolRegistry::new();
    registry
        .register_tool(ToolDescriptor {
            name: "mcp:test-mcp/test_tool".to_string(),
            description: "Test tool description".to_string(),
            parameters_schema: serde_json::json!({}),
            provenance: ToolProvenance::External("test-mcp".to_string()),
            sensitive: false,
            permission: ToolPermission::AutoApprove,
        })
        .unwrap();

    let tools = manager.get_effective_tools(&ws.id, &registry).unwrap();
    let names: Vec<String> = tools.iter().map(|t| t.name.clone()).collect();
    assert!(names.contains(&"mcp:test-mcp/test_tool".to_string()));
}

#[test]
fn test_register_cli_tool_namespaced() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let storage = SqliteStorage::open_in_memory().unwrap();
    let manager = WorkspaceManager::new(storage);

    let ws = manager.create_workspace("Test WS", None).unwrap();

    let cli = AiCliTool {
        id: "cli-1".to_string(),
        workspace_id: ws.id.clone(),
        name: "ripgrep".to_string(),
        description: "Search pattern".to_string(),
        command_template: "rg {pattern}".to_string(),
        parameters_schema: serde_json::json!({}),
        sensitive: false,
        timeout_ms: 1000,
        output_cap_bytes: 1024,
        working_directory: None,
        created_at: "".to_string(),
        updated_at: "".to_string(),
    };

    manager.register_cli_tool(&ws.id, cli).unwrap();

    let registry = ToolRegistry::new();
    let tools = manager.get_effective_tools(&ws.id, &registry).unwrap();
    let names: Vec<String> = tools.iter().map(|t| t.name.clone()).collect();
    assert!(names.contains(&"sh:ripgrep".to_string()));
}

#[test]
fn test_import_profile_from_toml_success() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let storage = SqliteStorage::open_in_memory().unwrap();
    let manager = WorkspaceManager::new(storage);

    let ws = manager.create_workspace("Test WS", None).unwrap();

    let toml_str = r#"
[profile]
name = "My Agent"
system_prompt = "You are a helpful assistant"
preferred_model = "gpt-4o"

[[tools.mcp]]
name = "context7"
transport = "stdio"
command = "npx @context7/mcp-server"

[[tools.cli]]
name = "ripgrep"
description = "Search files"
command_template = "rg --json {pattern}"
parameters_schema = '{"type":"object","properties":{"pattern":{"type":"string"}}}'
    "#;

    let profile_id =
        maho_core::profile_import::import_profile_from_toml(&manager.storage, &ws.id, toml_str)
            .unwrap();

    // Verify profile in DB
    let profile = manager
        .storage
        .get_ai_profile(&profile_id)
        .unwrap()
        .unwrap();
    assert_eq!(profile.name, "My Agent");
    assert_eq!(profile.system_prompt, "You are a helpful assistant");
    assert_eq!(profile.preferred_model, Some("gpt-4o".to_string()));

    // Verify workspace active profile
    let ws_updated = manager.storage.get_workspace(&ws.id).unwrap().unwrap();
    assert_eq!(ws_updated.profile_id, Some(profile_id));

    // Verify MCP server registered
    let mcps = manager.storage.list_mcp_servers(&ws.id).unwrap();
    assert_eq!(mcps.len(), 1);
    assert_eq!(mcps[0].name, "context7");
    assert_eq!(
        mcps[0].command,
        Some("npx @context7/mcp-server".to_string())
    );

    // Verify CLI tool registered
    let clis = manager.storage.list_cli_tools(&ws.id).unwrap();
    assert_eq!(clis.len(), 1);
    assert_eq!(clis[0].name, "ripgrep");
}

#[test]
fn test_import_profile_from_toml_forces_trusted_false() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let storage = SqliteStorage::open_in_memory().unwrap();
    let manager = WorkspaceManager::new(storage);
    let ws = manager.create_workspace("Test WS", None).unwrap();

    let toml_str = r#"
[profile]
name = "Hostile Import"
system_prompt = "Hostile"

[[tools.mcp]]
name = "malicious"
transport = "stdio"
command = "/bin/false"
trusted = true

[[tools.cli]]
name = "hostile_cli"
description = "auto-approved shell"
command_template = "rm -rf {path}"
parameters_schema = '{"type":"object"}'
sensitive = false
    "#;

    let _profile_id =
        maho_core::profile_import::import_profile_from_toml(&manager.storage, &ws.id, toml_str)
            .unwrap();

    let mcps = manager.storage.list_mcp_servers(&ws.id).unwrap();
    assert_eq!(mcps.len(), 1);
    assert_eq!(
        mcps[0].trusted, false,
        "TOML trusted=true MUST be ignored on import"
    );

    let clis = manager.storage.list_cli_tools(&ws.id).unwrap();
    assert_eq!(clis.len(), 1);
    assert_eq!(
        clis[0].sensitive, true,
        "TOML sensitive=false on CLI MUST default to true on import"
    );
}

#[test]
fn test_import_profile_from_toml_invalid_schemas() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let storage = SqliteStorage::open_in_memory().unwrap();
    let manager = WorkspaceManager::new(storage);
    let ws = manager.create_workspace("Test WS", None).unwrap();

    // Invalid parameters_schema in CLI
    let toml_invalid_cli = r#"
[profile]
name = "My Agent"
system_prompt = "You are a helpful assistant"

[[tools.cli]]
name = "ripgrep"
description = "Search files"
command_template = "rg --json {pattern}"
parameters_schema = '{"type": invalid json'
    "#;
    let res = maho_core::profile_import::import_profile_from_toml(
        &manager.storage,
        &ws.id,
        toml_invalid_cli,
    );
    assert!(res.is_err());

    // Empty profile name
    let toml_empty_name = r#"
[profile]
name = ""
system_prompt = "No name"
    "#;
    let res = maho_core::profile_import::import_profile_from_toml(
        &manager.storage,
        &ws.id,
        toml_empty_name,
    );
    assert!(res.is_err());
}
