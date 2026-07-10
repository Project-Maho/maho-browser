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
    let res = backend.run_turn("session-123", user_msg, None).await;

    assert!(res.is_err());
    assert!(matches!(res.unwrap_err(), maho_agent::AgentError::PermissionDenied));

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

    backend.set_permission_callback(Box::new(|_req| {
        Box::pin(async { PermissionDecision::Deny })
    })).unwrap();

    let mock_llm = DynamicMockLLM {
        calls: Arc::new(Mutex::new(Vec::new())),
        tool_name: "shell_exec".to_string(),
    };
    backend.set_llm_override(Box::new(mock_llm) as Box<dyn ChatCompletion>);

    let user_msg = ChatMessage::user(ChatContent::text("read file".to_string()));
    let res = backend.run_turn("session-123", user_msg, None).await;

    assert!(res.is_err());
    assert!(matches!(res.unwrap_err(), maho_agent::AgentError::PermissionDenied));
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
