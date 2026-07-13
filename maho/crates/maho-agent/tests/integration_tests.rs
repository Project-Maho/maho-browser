use std::sync::{Arc, Mutex};
use async_trait::async_trait;
use maho_agent::{AgentRuntime, AgentStorage, SwiftideBackend, PermissionDecision, PermissionRequest};
use maho_types::chat::{ChatMessage, ChatContent};
use swiftide_core::{
    chat_completion::{ChatCompletion, ChatCompletionRequest, ChatCompletionResponse, ToolCall, errors::LanguageModelError},
};

#[derive(Clone)]
struct MockAgentStorage {
    messages: Arc<Mutex<Vec<(String, String, String)>>>,
}

#[async_trait]
impl AgentStorage for MockAgentStorage {
    async fn get_byok_key(&self, _provider: &str) -> Result<Option<maho_agent::ZeroizedString>, maho_agent::AgentError> {
        Ok(Some(maho_agent::ZeroizedString::new("mock-key".to_string())))
    }

    async fn get_setting(&self, _key: &str) -> Result<Option<maho_agent::ZeroizedString>, maho_agent::AgentError> {
        Ok(None)
    }

    async fn save_message(&self, session_id: &str, role: &str, content: &str) -> Result<(), maho_agent::AgentError> {
        self.messages.lock().unwrap().push((session_id.to_string(), role.to_string(), content.to_string()));
        Ok(())
    }

    async fn get_history(&self, session_id: &str) -> Result<Vec<ChatMessage>, maho_agent::AgentError> {
        let lock = self.messages.lock().unwrap();
        let history: Vec<ChatMessage> = lock.iter()
            .filter(|(sid, _, _)| sid == session_id)
            .map(|(_, role, content)| {
                if role == "user" {
                    ChatMessage::user(ChatContent::text(content.clone()))
                } else if role == "assistant" {
                    ChatMessage::assistant(ChatContent::text(content.clone()))
                } else {
                    ChatMessage::system(ChatContent::text(content.clone()))
                }
            })
            .collect();
        Ok(history)
    }
}

#[derive(Clone)]
struct MockLLM {
    calls: Arc<Mutex<Vec<ChatCompletionRequest>>>,
    response: Arc<Mutex<ChatCompletionResponse>>,
}

#[async_trait]
impl ChatCompletion for MockLLM {
    async fn complete(
        &self,
        request: &ChatCompletionRequest,
    ) -> Result<ChatCompletionResponse, LanguageModelError> {
        self.calls.lock().unwrap().push(request.clone());
        Ok(self.response.lock().unwrap().clone())
    }
}

#[derive(Clone)]
struct DynamicMockLLM {
    calls: Arc<Mutex<Vec<ChatCompletionRequest>>>,
    tool_name: String,
}

#[async_trait]
impl ChatCompletion for DynamicMockLLM {
    async fn complete(
        &self,
        request: &ChatCompletionRequest,
    ) -> Result<ChatCompletionResponse, LanguageModelError> {
        let mut calls_lock = self.calls.lock().unwrap();
        calls_lock.push(request.clone());
        
        if calls_lock.len() == 1 {
            // First call: return a tool call
            let args = if self.tool_name == "shell_exec" {
                "{\"command\": \"ls\"}".to_string()
            } else {
                "{\"path\": \"/tmp/nonexistent\"}".to_string()
            };
            let tool_call = ToolCall::builder()
                .id("call_1".to_string())
                .name(self.tool_name.clone())
                .args(args)
                .build()
                .unwrap();
            Ok(ChatCompletionResponse::builder()
                .tool_calls(vec![tool_call])
                .build()
                .unwrap())
        } else {
            // Second call: return final text response
            Ok(ChatCompletionResponse::builder()
                .message("Done reading".to_string())
                .build()
                .unwrap())
        }
    }
}

#[tokio::test]
async fn test_agent_turn_integration() {
    let storage = Arc::new(MockAgentStorage {
        messages: Arc::new(Mutex::new(Vec::new())),
    });

    let backend = SwiftideBackend::new(
        Arc::clone(&storage) as Arc<dyn AgentStorage>,
        Some(Box::new(|_req| Box::pin(async { PermissionDecision::Allow }))),
        std::env::temp_dir(),
        true,
    );

    let mock_response = ChatCompletionResponse::builder()
        .message("Hello! I am a helpful assistant.".to_string())
        .build()
        .unwrap();

    let mock_llm = MockLLM {
        calls: Arc::new(Mutex::new(Vec::new())),
        response: Arc::new(Mutex::new(mock_response)),
    };

    backend.set_llm_override(Box::new(mock_llm) as Box<dyn ChatCompletion>);

    let user_msg = ChatMessage::user(ChatContent::text("Hello agent".to_string()));
    let response_msg = backend.run_turn("session-123", user_msg, None).await.unwrap();

    assert_eq!(response_msg.role.as_str(), "assistant");
    let content = match response_msg.content {
        ChatContent::Text(t) => t,
        _ => panic!("Expected text content"),
    };
    assert_eq!(content, "Hello! I am a helpful assistant.");

    let history = storage.get_history("session-123").await.unwrap();
    assert_eq!(history.len(), 2);
    assert_eq!(history[0].role.as_str(), "user");
    assert_eq!(history[1].role.as_str(), "assistant");
}

#[tokio::test]
async fn test_agent_tool_integration() {
    let storage = Arc::new(MockAgentStorage {
        messages: Arc::new(Mutex::new(Vec::new())),
    });

    let backend = SwiftideBackend::new(
        Arc::clone(&storage) as Arc<dyn AgentStorage>,
        Some(Box::new(|_req| Box::pin(async { PermissionDecision::Allow }))),
        std::env::temp_dir(),
        true,
    );

    let mock_llm = DynamicMockLLM {
        calls: Arc::new(Mutex::new(Vec::new())),
        tool_name: "fs_read".to_string(),
    };

    backend.set_llm_override(Box::new(mock_llm) as Box<dyn ChatCompletion>);

    let user_msg = ChatMessage::user(ChatContent::text("read file".to_string()));
    let response_msg = backend.run_turn("session-123", user_msg, None).await.unwrap();

    let content = match response_msg.content {
        ChatContent::Text(t) => t,
        _ => panic!("Expected text content"),
    };
    assert_eq!(content, "Done reading");
}

#[tokio::test]
async fn test_agent_permission_deny() {
    let storage = Arc::new(MockAgentStorage {
        messages: Arc::new(Mutex::new(Vec::new())),
    });

    let backend = SwiftideBackend::new(
        Arc::clone(&storage) as Arc<dyn AgentStorage>,
        None,
        std::env::temp_dir(),
        true,
    );

    let perm_call_count = Arc::new(Mutex::new(0usize));
    let perm_call_count_clone = Arc::clone(&perm_call_count);
    backend.set_permission_callback(Box::new(move |_req| {
        let count = Arc::clone(&perm_call_count_clone);
        Box::pin(async move {
            *count.lock().unwrap() += 1;
            PermissionDecision::Deny
        })
    })).unwrap();

    let mock_llm = DynamicMockLLM {
        calls: Arc::new(Mutex::new(Vec::new())),
        tool_name: "shell_exec".to_string(),
    };

    backend.set_llm_override(Box::new(mock_llm) as Box<dyn ChatCompletion>);

    let user_msg = ChatMessage::user(ChatContent::text("read file".to_string()));
    let response_msg = backend
        .run_turn("session-123", user_msg, None)
        .await
        .expect("turn completes: a denied tool returns a ToolError to the model (#2/#3-A), it does not abort the turn");

    let content = match response_msg.content {
        ChatContent::Text(t) => t,
        _ => panic!("Expected text content"),
    };
    assert_eq!(content, "Done reading");

    let count = *perm_call_count.lock().unwrap();
    assert_eq!(
        count, 1,
        "Permission callback must fire exactly once per turn; denied_flag short-circuits \
         all subsequent PermissionGatedTool::invoke calls within the same turn. \
         Got: {count} invocations (>1 indicates denied_flag retry-storm regression)",
    );
}

#[tokio::test]
async fn test_agent_fails_without_byok_key() {
    let storage = Arc::new(MockAgentStorage {
        messages: Arc::new(Mutex::new(Vec::new())),
    });

    let backend = SwiftideBackend::new(
        Arc::clone(&storage) as Arc<dyn AgentStorage>,
        None,
        std::env::temp_dir(),
        false, // allow_insecure_key_storage = false
    );

    let user_msg = ChatMessage::user(ChatContent::text("Hello".to_string()));
    let res = backend.run_turn("session-123", user_msg, None).await;

    assert!(res.is_err());
    let err_str = res.unwrap_err().to_string();
    assert!(err_str.contains("Secure storage callback is missing or returned no keys"));
}

#[tokio::test]
async fn test_agent_denies_unauthorized_tool() {
    let storage = Arc::new(MockAgentStorage {
        messages: Arc::new(Mutex::new(Vec::new())),
    });

    let backend = SwiftideBackend::new(
        Arc::clone(&storage) as Arc<dyn AgentStorage>,
        None,
        std::env::temp_dir(),
        true,
    );

    let perm_call_count = Arc::new(Mutex::new(0usize));
    let perm_call_count_clone = Arc::clone(&perm_call_count);
    backend.set_permission_callback(Box::new(move |_req| {
        let count = Arc::clone(&perm_call_count_clone);
        Box::pin(async move {
            *count.lock().unwrap() += 1;
            PermissionDecision::Deny
        })
    })).unwrap();

    let calls = Arc::new(Mutex::new(Vec::new()));
    let mock_llm = DynamicMockLLM {
        calls: Arc::clone(&calls),
        tool_name: "shell_exec".to_string(),
    };
    backend.set_llm_override(Box::new(mock_llm) as Box<dyn ChatCompletion>);

    let user_msg = ChatMessage::user(ChatContent::text("read file".to_string()));
    let response_msg = backend
        .run_turn("session-123", user_msg, None)
        .await
        .expect("turn completes: denied shell_exec returns a ToolError to the model (#2/#3-A)");

    let content = match response_msg.content {
        ChatContent::Text(t) => t,
        _ => panic!("Expected text content"),
    };
    assert_eq!(content, "Done reading");

    // Proof the tool was gated and never executed: PermissionGatedTool::invoke
    // consults the callback and, on Deny, returns a ToolError BEFORE calling
    // inner_tool.invoke(). So a fired-and-denied callback structurally
    // guarantees shell_exec did not run.
    let denied = *perm_call_count.lock().unwrap();
    assert_eq!(
        denied, 1,
        "permission callback must fire exactly once for the sensitive shell_exec request; \
         got {denied} (0 = tool bypassed the gate and may have executed)",
    );

    let recorded = calls.lock().unwrap();
    assert_eq!(
        recorded.len(),
        2,
        "the model is re-invoked after receiving the denial ToolError (call 1 requests the \
         tool, call 2 responds to the denial). Got {} LLM calls",
        recorded.len(),
    );
}

#[tokio::test]
async fn test_secure_storage_callback_invoked() {
    let storage = Arc::new(MockAgentStorage {
        messages: Arc::new(Mutex::new(Vec::new())),
    });

    let backend = SwiftideBackend::new(
        Arc::clone(&storage) as Arc<dyn AgentStorage>,
        None,
        std::env::temp_dir(),
        false, // allow_insecure_key_storage = false
    );

    let callback_invoked = Arc::new(Mutex::new(false));
    let callback_invoked_clone = Arc::clone(&callback_invoked);

    backend.set_secure_storage_callback(Box::new(move |provider| {
        if provider == "openai" {
            *callback_invoked_clone.lock().unwrap() = true;
            Ok(maho_agent::SecureKeyBundle {
                key: maho_agent::ZeroizedString::new("openai-key".to_string()),
                base_url: None,
                model: None,
            })
        } else {
            Err(maho_agent::AgentError::ExecutionError("not found".to_string()))
        }
    })).unwrap();

    let user_msg = ChatMessage::user(ChatContent::text("Hello".to_string()));
    let _ = backend.run_turn("session-123", user_msg, None).await;

    assert!(*callback_invoked.lock().unwrap());
}

#[tokio::test]
async fn test_secure_storage_callback_failure_fails_closed() {
    let storage = Arc::new(MockAgentStorage {
        messages: Arc::new(Mutex::new(Vec::new())),
    });

    let backend = SwiftideBackend::new(
        Arc::clone(&storage) as Arc<dyn AgentStorage>,
        None,
        std::env::temp_dir(),
        false,
    );

    backend.set_secure_storage_callback(Box::new(move |_provider| {
        Err(maho_agent::AgentError::ExecutionError("simulated keychain failure".to_string()))
    })).unwrap();

    let user_msg = ChatMessage::user(ChatContent::text("Hello".to_string()));
    let res = backend.run_turn("session-fail", user_msg, None).await;

    assert!(res.is_err(), "Agent must fail-closed when secure storage returns error");
    let err_str = res.unwrap_err().to_string();
    assert!(
        err_str.contains("Secure storage callback") || err_str.contains("No BYOK API key"),
        "Error must indicate secure storage / BYOK problem; got: {err_str}",
    );
}

#[tokio::test]
async fn test_secure_storage_callback_provider_routing() {
    let storage = Arc::new(MockAgentStorage {
        messages: Arc::new(Mutex::new(Vec::new())),
    });

    let backend = SwiftideBackend::new(
        Arc::clone(&storage) as Arc<dyn AgentStorage>,
        None,
        std::env::temp_dir(),
        false,
    );

    let invocations = Arc::new(Mutex::new(Vec::<String>::new()));
    let invocations_clone = Arc::clone(&invocations);

    backend.set_secure_storage_callback(Box::new(move |provider| {
        invocations_clone.lock().unwrap().push(provider.to_string());
        if provider == "anthropic" {
            Ok(maho_agent::SecureKeyBundle {
                key: maho_agent::ZeroizedString::new("anthropic-key".to_string()),
                base_url: None,
                model: None,
            })
        } else {
            Err(maho_agent::AgentError::ExecutionError("no key for provider".to_string()))
        }
    })).unwrap();

    let user_msg = ChatMessage::user(ChatContent::text("Hello".to_string()));
    let _ = backend.run_turn("session-routing", user_msg, None).await;

    let calls = invocations.lock().unwrap().clone();
    assert!(calls.contains(&"openai".to_string()),
        "Backend must try 'openai' provider; got calls: {calls:?}");
    assert!(calls.contains(&"anthropic".to_string()),
        "Backend must fall through to 'anthropic' when 'openai' fails; got calls: {calls:?}");
}

#[tokio::test]
async fn test_byok_key_access_audit_emitted() {
    use std::sync::atomic::{AtomicBool, Ordering};

    let storage = Arc::new(MockAgentStorage {
        messages: Arc::new(Mutex::new(Vec::new())),
    });

    let backend = SwiftideBackend::new(
        Arc::clone(&storage) as Arc<dyn AgentStorage>,
        None,
        std::env::temp_dir(),
        false,
    );

    let observed = Arc::new(AtomicBool::new(false));
    let observed_clone = Arc::clone(&observed);

    backend.set_secure_storage_callback(Box::new(move |provider| {
        if provider == "openai" {
            observed_clone.store(true, Ordering::SeqCst);
            Ok(maho_agent::SecureKeyBundle {
                key: maho_agent::ZeroizedString::new("audit-test-key".to_string()),
                base_url: None,
                model: None,
            })
        } else {
            Err(maho_agent::AgentError::ExecutionError("not found".to_string()))
        }
    })).unwrap();

    let user_msg = ChatMessage::user(ChatContent::text("Hello".to_string()));
    let _ = backend.run_turn("session-audit", user_msg, None).await;

    assert!(observed.load(Ordering::SeqCst),
        "Secure storage callback must be invoked at least once for openai provider");
}

#[tokio::test]
async fn test_agent_system_prompt_integration() {
    let storage = Arc::new(MockAgentStorage {
        messages: Arc::new(Mutex::new(Vec::new())),
    });

    let backend = SwiftideBackend::new(
        Arc::clone(&storage) as Arc<dyn AgentStorage>,
        Some(Box::new(|_req| Box::pin(async { PermissionDecision::Allow }))),
        std::env::temp_dir(),
        true,
    );

    let mock_response = ChatCompletionResponse::builder()
        .message("Hello! I am a helpful assistant.".to_string())
        .build()
        .unwrap();

    let mock_llm = MockLLM {
        calls: Arc::new(Mutex::new(Vec::new())),
        response: Arc::new(Mutex::new(mock_response)),
    };

    backend.set_llm_override(Box::new(mock_llm.clone()) as Box<dyn ChatCompletion>);
    backend.set_system_prompt("You are a code reviewer");

    let user_msg = ChatMessage::user(ChatContent::text("Hello agent".to_string()));
    let _response_msg = backend.run_turn("session-123", user_msg, None).await.unwrap();

    // Check that system prompt was prepended to the LLM call history
    let calls = mock_llm.calls.lock().unwrap();
    assert_eq!(calls.len(), 1);
    let messages = &calls[0].messages;
    assert!(messages.len() >= 2);
    assert!(matches!(&messages[0], swiftide_core::chat_completion::ChatMessage::System(prompt) if prompt == "You are a code reviewer"));

    // Check that system prompt is NOT present in the persisted history
    let history = storage.get_history("session-123").await.unwrap();
    assert_eq!(history.len(), 2);
    assert_eq!(history[0].role.as_str(), "user");
    assert_eq!(history[1].role.as_str(), "assistant");
}

#[tokio::test]
async fn test_mcp_client_stdio_integration() {
    use maho_types::ai::{AiMcpServer, McpTransport};
    use maho_agent::mcp_client::McpClient;
    use serde_json::json;

    // Write mock MCP server script to a temp file
    let temp_dir = tempfile::tempdir().unwrap();
    let script_path = temp_dir.path().join("mock_mcp_server.py");
    let script_content = r#"import sys
import json

def main():
    while True:
        line = sys.stdin.readline()
        if not line:
            break
        try:
            req = json.loads(line)
        except Exception:
            continue
        
        method = req.get("method")
        req_id = req.get("id")
        
        if method == "initialize":
            resp = {
                "jsonrpc": "2.0",
                "id": req_id,
                "result": {
                    "protocolVersion": "2024-11-05",
                    "capabilities": {},
                    "serverInfo": {
                        "name": "mock-python-server",
                        "version": "1.0"
                    }
                }
            }
        elif method == "tools/list":
            resp = {
                "jsonrpc": "2.0",
                "id": req_id,
                "result": {
                    "tools": [
                        {
                            "name": "echo",
                            "description": "Echoes back the input message",
                            "inputSchema": {
                                "type": "object",
                                "properties": {
                                    "message": {"type": "string"}
                                },
                                "required": ["message"]
                            }
                        }
                    ]
                }
            }
        elif method == "tools/call":
            params = req.get("params", {})
            arguments = params.get("arguments", {})
            msg = arguments.get("message", "")
            
            resp = {
                "jsonrpc": "2.0",
                "id": req_id,
                "result": {
                    "content": [
                        {
                            "type": "text",
                            "text": f"Echo: {msg}"
                        }
                    ],
                    "isError": False
                }
            }
        else:
            if req_id is not None:
                resp = {
                    "jsonrpc": "2.0",
                    "id": req_id,
                    "result": {}
                }
            else:
                continue
                
        sys.stdout.write(json.dumps(resp) + "\n")
        sys.stdout.flush()

if __name__ == "__main__":
    main()
"#;

    tokio::fs::write(&script_path, script_content).await.unwrap();

    // Configure commands to run the script via python3
    let command_json = format!("[\"python3\", \"{}\"]", script_path.display());

    let server_config = AiMcpServer {
        id: "mock-server-123".to_string(),
        workspace_id: "workspace-123".to_string(),
        name: "mock-server".to_string(),
        transport: McpTransport::Stdio,
        command: Some(command_json),
        url: None,
        auth_keychain_id: None,
        trusted: true,
        trusted_tools: None,
        timeout_ms: 60000,
        output_cap_bytes: 10240,
        socket_path: None,
        created_at: "".to_string(),
        updated_at: "".to_string(),
    };

    let client = McpClient::connect(&server_config, None::<String>).await.unwrap();

    // 1. Test listing tools
    let tools = client.list_tools().await.unwrap();
    assert_eq!(tools.len(), 1);
    assert_eq!(tools[0].name, "mcp:mock-server/echo");
    assert_eq!(tools[0].description, "Echoes back the input message");

    // 2. Test calling tool
    let args = json!({ "message": "hello from test" });
    let result = client.call_tool("mcp:mock-server/echo", args).await.unwrap();
    
    // Check that we got the expected Echo response inside CallToolResult
    let content = result.get("content").unwrap().as_array().unwrap();
    assert_eq!(content.len(), 1);
    let text = content[0].get("text").unwrap().as_str().unwrap();
    assert_eq!(text, "Echo: hello from test");

    // 3. Test truncation (R-2)
    let mut small_server_config = server_config.clone();
    small_server_config.output_cap_bytes = 10;
    let small_client = McpClient::connect(&small_server_config, None::<String>).await.unwrap();
    let args = json!({ "message": "this_is_a_very_long_message" });
    let result = small_client.call_tool("mcp:mock-server/echo", args).await.unwrap();
    let content = result.get("content").unwrap().as_array().unwrap();
    let text = content[0].get("text").unwrap().as_str().unwrap();
    assert!(text.contains("output truncated"));
    assert!(text.len() <= 10 + "... [output truncated]".len());

    // 4. Test empty tools error (R-3)
    let empty_script_path = temp_dir.path().join("empty_mock_mcp.py");
    let empty_script_content = r#"
import sys
import json

def main():
    while True:
        line = sys.stdin.readline()
        if not line:
            break
        req = json.loads(line)
        req_id = req.get("id")
        method = req.get("method")
        
        if method == "initialize":
            resp = {
                "jsonrpc": "2.0",
                "id": req_id,
                "result": {
                    "protocolVersion": "2025-03-26",
                    "serverInfo": {"name": "empty-mock", "version": "0.1.0"},
                    "capabilities": {}
                }
            }
        elif method == "tools/list":
            resp = {
                "jsonrpc": "2.0",
                "id": req_id,
                "result": {
                    "tools": []
                }
            }
        else:
            if req_id is not None:
                resp = {
                    "jsonrpc": "2.0",
                    "id": req_id,
                    "result": {}
                }
            else:
                continue
                
        sys.stdout.write(json.dumps(resp) + "\n")
        sys.stdout.flush()

if __name__ == "__main__":
    main()
"#;
    tokio::fs::write(&empty_script_path, empty_script_content).await.unwrap();
    let empty_command_json = format!("[\"python3\", \"{}\"]", empty_script_path.display());
    let mut empty_server_config = server_config.clone();
    empty_server_config.command = Some(empty_command_json);
    empty_server_config.name = "empty-mock".to_string();
    
    let empty_client = McpClient::connect(&empty_server_config, None::<String>).await.unwrap();
    let err = empty_client.list_tools().await.unwrap_err();
    assert!(err.to_string().contains("server exposes no callable tools"));
}

#[tokio::test]
async fn test_cli_tool_executor_integration() {
    use maho_types::ai::AiCliTool;
    use maho_agent::cli_tool_executor::CliToolExecutor;
    use serde_json::json;

    let tool_config = AiCliTool {
        id: "cli-tool-123".to_string(),
        workspace_id: "workspace-123".to_string(),
        name: "test_echo".to_string(),
        description: "Echoes a value".to_string(),
        command_template: "echo {message}".to_string(),
        parameters_schema: json!({
            "type": "object",
            "properties": {
                "message": { "type": "string" }
            },
            "required": ["message"]
        }),
        sensitive: false,
        timeout_ms: 5000,
        output_cap_bytes: 1024,
        working_directory: None,
        created_at: "".to_string(),
        updated_at: "".to_string(),
    };

    let executor = CliToolExecutor::new(tool_config, None);

    // 1. Valid invocation
    let args = json!({ "message": "hello CLI" });
    let result = executor.execute(args).await.unwrap();
    
    let stdout = result.get("stdout").unwrap().as_str().unwrap().trim();
    assert_eq!(stdout, "hello CLI");
    assert_eq!(result.get("exit_code").unwrap().as_i64().unwrap(), 0);

    // 2. Missing required parameter validation
    let invalid_args = json!({});
    let err = executor.execute(invalid_args).await.unwrap_err();
    assert!(err.to_string().contains("Missing required parameter"));

    // 3. Wrong type validation
    let wrong_type_args = json!({ "message": 123 });
    let err2 = executor.execute(wrong_type_args).await.unwrap_err();
    assert!(err2.to_string().contains("must be a string"));
}

// Storage that returns one workspace bound to a profile with a configurable
// tool allowlist, so run_turn_internal exercises profile.tools scoping.
#[derive(Clone)]
struct ProfileScopingStorage {
    messages: Arc<Mutex<Vec<(String, String, String)>>>,
    root: String,
    profile_tools: Vec<String>,
}

#[async_trait]
impl AgentStorage for ProfileScopingStorage {
    async fn get_byok_key(&self, _provider: &str) -> Result<Option<maho_agent::ZeroizedString>, maho_agent::AgentError> {
        Ok(Some(maho_agent::ZeroizedString::new("mock-key".to_string())))
    }
    async fn get_setting(&self, _key: &str) -> Result<Option<maho_agent::ZeroizedString>, maho_agent::AgentError> {
        Ok(None)
    }
    async fn save_message(&self, session_id: &str, role: &str, content: &str) -> Result<(), maho_agent::AgentError> {
        self.messages.lock().unwrap().push((session_id.to_string(), role.to_string(), content.to_string()));
        Ok(())
    }
    async fn get_history(&self, _session_id: &str) -> Result<Vec<ChatMessage>, maho_agent::AgentError> {
        Ok(vec![])
    }
    async fn list_workspaces(&self) -> Result<Vec<maho_types::ai::AiWorkspace>, maho_agent::AgentError> {
        Ok(vec![maho_types::ai::AiWorkspace {
            id: "ws1".to_string(),
            name: "WS".to_string(),
            profile_id: Some("prof1".to_string()),
            space_id: None,
            workspace_root: Some(self.root.clone()),
            created_at: String::new(),
            updated_at: String::new(),
        }])
    }
    async fn get_profile(&self, id: &str) -> Result<Option<maho_types::ai::AiProfile>, maho_agent::AgentError> {
        if id == "prof1" {
            Ok(Some(maho_types::ai::AiProfile {
                id: "prof1".to_string(),
                name: "P".to_string(),
                system_prompt: String::new(),
                preferred_model: None,
                tools: self.profile_tools.clone(),
                mcp_servers: vec![],
                is_default: false,
                created_at: String::new(),
                updated_at: String::new(),
            }))
        } else {
            Ok(None)
        }
    }
}

// LLM that records the tool names offered in the request, then ends the turn.
#[derive(Clone)]
struct CapturingLLM {
    tool_names: Arc<Mutex<Vec<String>>>,
}

#[async_trait]
impl ChatCompletion for CapturingLLM {
    async fn complete(
        &self,
        request: &ChatCompletionRequest,
    ) -> Result<ChatCompletionResponse, LanguageModelError> {
        let mut names: Vec<String> = request.tools_spec().iter().map(|t| t.name.clone()).collect();
        names.sort();
        *self.tool_names.lock().unwrap() = names;
        Ok(ChatCompletionResponse::builder()
            .message("done".to_string())
            .build()
            .unwrap())
    }
}

async fn run_turn_capturing_tools(profile_tools: Vec<String>) -> Vec<String> {
    let root = std::env::temp_dir().to_string_lossy().to_string();
    let storage = Arc::new(ProfileScopingStorage {
        messages: Arc::new(Mutex::new(Vec::new())),
        root,
        profile_tools,
    });
    let backend = SwiftideBackend::new(
        Arc::clone(&storage) as Arc<dyn AgentStorage>,
        Some(Box::new(|_req| Box::pin(async { PermissionDecision::Allow }))),
        std::env::temp_dir(),
        true,
    );
    let captured = Arc::new(Mutex::new(Vec::new()));
    backend.set_llm_override(Box::new(CapturingLLM { tool_names: Arc::clone(&captured) }) as Box<dyn ChatCompletion>);

    let user_msg = ChatMessage::user(ChatContent::text("hi".to_string()));
    backend.run_turn("s1", user_msg, None).await.unwrap();
    let names = captured.lock().unwrap().clone();
    names
}

#[tokio::test]
async fn test_profile_tools_allowlist_filters_exec_tools() {
    let names = run_turn_capturing_tools(vec!["fs_read".to_string()]).await;
    assert!(
        names.iter().any(|n| n == "fs_read"),
        "fs_read is in the profile allowlist and must be offered; got {names:?}",
    );
    assert!(
        !names.iter().any(|n| n == "shell_exec"),
        "shell_exec is NOT in the profile allowlist and must be filtered out at exec time; got {names:?}",
    );
}

#[tokio::test]
async fn test_profile_tools_empty_allows_all() {
    let names = run_turn_capturing_tools(vec![]).await;
    assert!(
        names.iter().any(|n| n == "fs_read") && names.iter().any(|n| n == "shell_exec"),
        "an empty profile.tools allowlist must allow all tools; got {names:?}",
    );
}

// Requests fs_read on `path` on the first call, then captures the full request
// (including the tool result folded into history) on the second call.
#[derive(Clone)]
struct FsReadCaptureLLM {
    call_count: Arc<Mutex<usize>>,
    path: String,
    captured: Arc<Mutex<String>>,
}

#[async_trait]
impl ChatCompletion for FsReadCaptureLLM {
    async fn complete(
        &self,
        request: &ChatCompletionRequest,
    ) -> Result<ChatCompletionResponse, LanguageModelError> {
        let mut n = self.call_count.lock().unwrap();
        *n += 1;
        if *n == 1 {
            let tc = ToolCall::builder()
                .id("call_1".to_string())
                .name("fs_read".to_string())
                .args(format!("{{\"path\": \"{}\"}}", self.path))
                .build()
                .unwrap();
            Ok(ChatCompletionResponse::builder().tool_calls(vec![tc]).build().unwrap())
        } else {
            *self.captured.lock().unwrap() = format!("{request:?}");
            Ok(ChatCompletionResponse::builder().message("done".to_string()).build().unwrap())
        }
    }
}

async fn run_fsread_turn(policy: Option<&str>) -> String {
    let dir = std::env::temp_dir();
    let path = dir.join(format!("maho_fsread_probe_{}.txt", std::process::id()));
    std::fs::write(&path, b"SECRET_CONTENT_XYZ").unwrap();

    let storage = Arc::new(MockAgentStorage {
        messages: Arc::new(Mutex::new(Vec::new())),
    });
    let backend = SwiftideBackend::new(
        Arc::clone(&storage) as Arc<dyn AgentStorage>,
        Some(Box::new(|_req| Box::pin(async { PermissionDecision::Allow }))),
        dir.clone(),
        true,
    );
    if let Some(p) = policy {
        backend.set_approval_policy(p.to_string());
    }
    let captured = Arc::new(Mutex::new(String::new()));
    backend.set_llm_override(Box::new(FsReadCaptureLLM {
        call_count: Arc::new(Mutex::new(0)),
        path: path.to_string_lossy().to_string(),
        captured: Arc::clone(&captured),
    }) as Box<dyn ChatCompletion>);

    let user_msg = ChatMessage::user(ChatContent::text("read it".to_string()));
    backend.run_turn("s1", user_msg, None).await.unwrap();
    let _ = std::fs::remove_file(&path);
    let out = captured.lock().unwrap().clone();
    out
}

#[tokio::test]
async fn test_approval_policy_deny_blocks_nonsensitive_tool() {
    // Positive control: with no deny policy, fs_read (non-sensitive, bypasses
    // the per-tool permission callback) executes and its content reaches the
    // model on the follow-up call.
    let allowed = run_fsread_turn(None).await;
    assert!(
        allowed.contains("SECRET_CONTENT_XYZ"),
        "sanity: fs_read should run without a deny policy; got: {allowed}",
    );

    // #7: a global "deny" policy must block even a non-sensitive tool, so the
    // tool never executes and its content never reaches the model.
    let denied = run_fsread_turn(Some("deny")).await;
    assert!(
        !denied.contains("SECRET_CONTENT_XYZ"),
        "global deny must block fs_read before execution; content leaked: {denied}",
    );
}

// Storage that returns a workspace/profile with a distinct "resolved" system
// prompt AND a distinct persisted snapshot, so the #13 precedence can be
// observed unambiguously.
#[derive(Clone)]
struct SnapshotStorage {
    root: String,
}

#[async_trait]
impl AgentStorage for SnapshotStorage {
    async fn get_byok_key(&self, _p: &str) -> Result<Option<maho_agent::ZeroizedString>, maho_agent::AgentError> {
        Ok(Some(maho_agent::ZeroizedString::new("mock-key".to_string())))
    }
    async fn get_setting(&self, _k: &str) -> Result<Option<maho_agent::ZeroizedString>, maho_agent::AgentError> {
        Ok(None)
    }
    async fn save_message(&self, _s: &str, _r: &str, _c: &str) -> Result<(), maho_agent::AgentError> {
        Ok(())
    }
    async fn get_history(&self, _s: &str) -> Result<Vec<ChatMessage>, maho_agent::AgentError> {
        Ok(vec![])
    }
    async fn list_workspaces(&self) -> Result<Vec<maho_types::ai::AiWorkspace>, maho_agent::AgentError> {
        Ok(vec![maho_types::ai::AiWorkspace {
            id: "ws1".to_string(),
            name: "WS".to_string(),
            profile_id: Some("prof1".to_string()),
            space_id: None,
            workspace_root: Some(self.root.clone()),
            created_at: String::new(),
            updated_at: String::new(),
        }])
    }
    async fn get_profile(&self, id: &str) -> Result<Option<maho_types::ai::AiProfile>, maho_agent::AgentError> {
        if id == "prof1" {
            Ok(Some(maho_types::ai::AiProfile {
                id: "prof1".to_string(),
                name: "P".to_string(),
                system_prompt: "RESOLVED_PROMPT_MARKER".to_string(),
                preferred_model: None,
                tools: vec![],
                mcp_servers: vec![],
                is_default: false,
                created_at: String::new(),
                updated_at: String::new(),
            }))
        } else {
            Ok(None)
        }
    }
    async fn get_system_prompt_snapshot(&self, _c: &str) -> Result<Option<String>, maho_agent::AgentError> {
        Ok(Some("SNAPSHOT_PROMPT_MARKER".to_string()))
    }
}

#[derive(Clone)]
struct FirstRequestDumpLLM {
    dump: Arc<Mutex<String>>,
}

#[async_trait]
impl ChatCompletion for FirstRequestDumpLLM {
    async fn complete(
        &self,
        request: &ChatCompletionRequest,
    ) -> Result<ChatCompletionResponse, LanguageModelError> {
        *self.dump.lock().unwrap() = format!("{request:?}");
        Ok(ChatCompletionResponse::builder().message("done".to_string()).build().unwrap())
    }
}

#[tokio::test]
async fn test_system_prompt_snapshot_takes_precedence_over_resolved() {
    let root = std::env::temp_dir().to_string_lossy().to_string();
    let storage = Arc::new(SnapshotStorage { root });
    let backend = SwiftideBackend::new(
        Arc::clone(&storage) as Arc<dyn AgentStorage>,
        Some(Box::new(|_req| Box::pin(async { PermissionDecision::Allow }))),
        std::env::temp_dir(),
        true,
    );
    // No set_system_prompt override, so precedence is snapshot > resolved.
    let dump = Arc::new(Mutex::new(String::new()));
    backend.set_llm_override(Box::new(FirstRequestDumpLLM { dump: Arc::clone(&dump) }) as Box<dyn ChatCompletion>);

    let user_msg = ChatMessage::user(ChatContent::text("hi".to_string()));
    backend.run_turn("s1", user_msg, None).await.unwrap();

    let seen = dump.lock().unwrap().clone();
    assert!(
        seen.contains("SNAPSHOT_PROMPT_MARKER"),
        "#13: persisted snapshot must be used as the system prompt; got {seen}",
    );
    assert!(
        !seen.contains("RESOLVED_PROMPT_MARKER"),
        "#13: snapshot must take precedence over the freshly resolved profile prompt; got {seen}",
    );
}

// Storage whose workspace exposes two trusted MCP servers (srv-a, srv-b) with
// offline-shim fallback tools, and a profile that allowlists only srv-a.
#[derive(Clone)]
struct McpScopingStorage {
    root: String,
}

fn make_mcp_server(id: &str, name: &str) -> maho_types::ai::AiMcpServer {
    maho_types::ai::AiMcpServer {
        id: id.to_string(),
        workspace_id: "ws1".to_string(),
        name: name.to_string(),
        transport: maho_types::ai::McpTransport::Stdio,
        // Nonexistent binary → connect fails fast → offline-shim tools are added
        // for trusted_tools, which is what we observe.
        command: Some("/nonexistent-maho-test-binary".to_string()),
        url: None,
        auth_keychain_id: None,
        trusted: true,
        trusted_tools: Some(vec!["theTool".to_string()]),
        timeout_ms: 2000,
        output_cap_bytes: 10240,
        socket_path: None,
        created_at: String::new(),
        updated_at: "0".to_string(),
    }
}

#[async_trait]
impl AgentStorage for McpScopingStorage {
    async fn get_byok_key(&self, _p: &str) -> Result<Option<maho_agent::ZeroizedString>, maho_agent::AgentError> {
        Ok(Some(maho_agent::ZeroizedString::new("mock-key".to_string())))
    }
    async fn get_setting(&self, _k: &str) -> Result<Option<maho_agent::ZeroizedString>, maho_agent::AgentError> {
        Ok(None)
    }
    async fn save_message(&self, _s: &str, _r: &str, _c: &str) -> Result<(), maho_agent::AgentError> {
        Ok(())
    }
    async fn get_history(&self, _s: &str) -> Result<Vec<ChatMessage>, maho_agent::AgentError> {
        Ok(vec![])
    }
    async fn list_workspaces(&self) -> Result<Vec<maho_types::ai::AiWorkspace>, maho_agent::AgentError> {
        Ok(vec![maho_types::ai::AiWorkspace {
            id: "ws1".to_string(),
            name: "WS".to_string(),
            profile_id: Some("prof1".to_string()),
            space_id: None,
            workspace_root: Some(self.root.clone()),
            created_at: String::new(),
            updated_at: String::new(),
        }])
    }
    async fn get_profile(&self, id: &str) -> Result<Option<maho_types::ai::AiProfile>, maho_agent::AgentError> {
        if id == "prof1" {
            Ok(Some(maho_types::ai::AiProfile {
                id: "prof1".to_string(),
                name: "P".to_string(),
                system_prompt: String::new(),
                preferred_model: None,
                tools: vec![],
                mcp_servers: vec!["srv-a".to_string()],
                is_default: false,
                created_at: String::new(),
                updated_at: String::new(),
            }))
        } else {
            Ok(None)
        }
    }
    async fn list_mcp_servers(&self, _ws: &str) -> Result<Vec<maho_types::ai::AiMcpServer>, maho_agent::AgentError> {
        Ok(vec![make_mcp_server("srv-a", "SrvA"), make_mcp_server("srv-b", "SrvB")])
    }
}

#[tokio::test]
async fn test_profile_mcp_servers_scoping() {
    let root = std::env::temp_dir().to_string_lossy().to_string();
    let storage = Arc::new(McpScopingStorage { root });
    let backend = SwiftideBackend::new(
        Arc::clone(&storage) as Arc<dyn AgentStorage>,
        Some(Box::new(|_req| Box::pin(async { PermissionDecision::Allow }))),
        std::env::temp_dir(),
        true,
    );
    let captured = Arc::new(Mutex::new(Vec::new()));
    backend.set_llm_override(Box::new(CapturingLLM { tool_names: Arc::clone(&captured) }) as Box<dyn ChatCompletion>);

    let user_msg = ChatMessage::user(ChatContent::text("hi".to_string()));
    backend.run_turn("s1", user_msg, None).await.unwrap();
    let names = captured.lock().unwrap().clone();

    // srv-a is in profile.mcp_servers → attempted → offline-shim tool offered.
    assert!(
        names.iter().any(|n| n == "mcp:SrvA/theTool"),
        "allowlisted MCP server srv-a must contribute its tool; got {names:?}",
    );
    // srv-b is NOT allowlisted → skipped entirely → no tool.
    assert!(
        !names.iter().any(|n| n == "mcp:SrvB/theTool"),
        "non-allowlisted MCP server srv-b must be filtered out; got {names:?}",
    );
}

#[tokio::test]
async fn test_mcp_client_is_alive_transitions() {
    use maho_types::ai::{AiMcpServer, McpTransport};
    use maho_agent::mcp_client::McpClient;
    use std::time::Duration;

    let temp_dir = tempfile::tempdir().unwrap();
    let script_path = temp_dir.path().join("min_mcp_server.py");
    let script = r#"import sys, json
def main():
    while True:
        line = sys.stdin.readline()
        if not line:
            break
        try:
            req = json.loads(line)
        except Exception:
            continue
        method = req.get("method")
        req_id = req.get("id")
        if method == "initialize":
            resp = {"jsonrpc":"2.0","id":req_id,"result":{"protocolVersion":"2024-11-05","capabilities":{},"serverInfo":{"name":"min","version":"1.0"}}}
        elif req_id is not None:
            resp = {"jsonrpc":"2.0","id":req_id,"result":{}}
        else:
            continue
        sys.stdout.write(json.dumps(resp)+"\n"); sys.stdout.flush()
if __name__ == "__main__":
    main()
"#;
    tokio::fs::write(&script_path, script).await.unwrap();
    let command_json = format!("[\"python3\", \"{}\"]", script_path.display());

    let server_config = AiMcpServer {
        id: "live-1".to_string(),
        workspace_id: "w".to_string(),
        name: "live".to_string(),
        transport: McpTransport::Stdio,
        command: Some(command_json),
        url: None,
        auth_keychain_id: None,
        trusted: true,
        trusted_tools: None,
        timeout_ms: 60000,
        output_cap_bytes: 10240,
        socket_path: None,
        created_at: String::new(),
        updated_at: String::new(),
    };

    let mut client = McpClient::connect(&server_config, None::<String>).await.unwrap();
    // #12: a freshly connected client's transport is open.
    assert!(client.is_alive(), "freshly connected MCP client must report alive");

    client.disconnect().await;

    // disconnect drops the RunningService; the background task terminates and
    // closes the peer channel. Poll until is_transport_closed() propagates.
    let mut became_dead = false;
    for _ in 0..100 {
        if !client.is_alive() {
            became_dead = true;
            break;
        }
        tokio::time::sleep(Duration::from_millis(20)).await;
    }
    assert!(
        became_dead,
        "#12: is_alive() must become false once the transport closes after disconnect",
    );
}
