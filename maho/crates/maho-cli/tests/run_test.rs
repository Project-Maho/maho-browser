use std::process::Command;
use tempfile::NamedTempFile;

fn run_cli(
    args: &[&str],
    db_path: &std::path::Path,
    extra_env: &[(&str, &str)],
) -> std::process::Output {
    let bin = env!("CARGO_BIN_EXE_maho");
    let mut cmd = Command::new(bin);
    cmd.args(args);
    cmd.env("MAHO_DB_PATH", db_path);
    for (k, v) in extra_env {
        cmd.env(k, v);
    }
    cmd.output().expect("failed to run CLI binary")
}

fn setup_db() -> (NamedTempFile, std::path::PathBuf) {
    let db_file = NamedTempFile::new().unwrap();
    let db_path = db_file.path().to_path_buf();

    // Initialize default workspace
    let storage = maho_storage::sqlite::SqliteStorage::open(&db_path.to_string_lossy()).unwrap();
    let ws = maho_types::ai::AiWorkspace {
        id: "default".to_string(),
        name: "Default Workspace".to_string(),
        space_id: None,
        profile_id: Some("blank".to_string()),
        workspace_root: None,
        created_at: "".to_string(),
        updated_at: "".to_string(),
    };
    storage.create_workspace(&ws).unwrap();

    (db_file, db_path)
}

#[test]
fn test_run_command_missing_keys() {
    let (_db_file, db_path) = setup_db();

    // Running without keys should fail with "No API key configured" or similar
    let output = run_cli(&["run", "say hello"], &db_path, &[]);
    let stderr = String::from_utf8_lossy(&output.stderr);
    let stdout = String::from_utf8_lossy(&output.stdout);

    assert!(
        stderr.contains("No BYOK API key configured")
            || stderr.contains("No API key found")
            || stdout.contains("Agent loop failed")
    );
}

#[test]
fn test_run_command_with_mock_key() {
    let (_db_file, db_path) = setup_db();

    // Set environment variable to bypass key validation on startup, and trigger the agent loop
    let output = run_cli(
        &["run", "say hello", "--provider", "openai"],
        &db_path,
        &[("OPENAI_API_KEY", "mock-key-123")],
    );
    let stderr = String::from_utf8_lossy(&output.stderr);
    let stdout = String::from_utf8_lossy(&output.stdout);

    // It should print "Running autonomous agent session..."
    assert!(stdout.contains("Running autonomous agent session"));
    // Since the key is mock, it will fail when hitting OpenAI, which is expected
    assert!(
        stderr.contains("OpenAI")
            || stderr.contains("401")
            || stderr.contains("Unauthorized")
            || stderr.contains("Agent loop failed")
            || stdout.contains("Agent loop failed")
    );
}

mod agent_loop {
    use std::sync::{Arc, Mutex};
    use std::time::Duration;

    use async_trait::async_trait;
    use maho_agent::{
        AgentError, AgentRuntime, AgentStorage, BrowserToolBridge, PermissionDecision,
        SwiftideBackend, ZeroizedString,
    };
    use maho_types::chat::ChatMessage;
    use swiftide_core::chat_completion::{
        errors::LanguageModelError, ChatCompletion, ChatCompletionRequest, ChatCompletionResponse,
        ToolCall,
    };

    #[derive(Clone, Default)]
    struct MockStorage {
        messages: Arc<Mutex<Vec<(String, String, String)>>>,
    }

    #[async_trait]
    impl AgentStorage for MockStorage {
        async fn get_byok_key(&self, _p: &str) -> Result<Option<ZeroizedString>, AgentError> {
            Ok(None)
        }
        async fn get_setting(&self, _k: &str) -> Result<Option<ZeroizedString>, AgentError> {
            Ok(None)
        }
        async fn save_message(&self, s: &str, r: &str, c: &str) -> Result<(), AgentError> {
            self.messages
                .lock()
                .unwrap()
                .push((s.to_string(), r.to_string(), c.to_string()));
            Ok(())
        }
        async fn get_history(&self, _s: &str) -> Result<Vec<ChatMessage>, AgentError> {
            Ok(vec![])
        }
    }

    #[derive(Clone, Default)]
    struct RecordingBridge {
        calls: Arc<Mutex<Vec<String>>>,
    }

    impl BrowserToolBridge for RecordingBridge {
        fn invoke(&self, tool_name: &str, _args: &str) -> Result<String, String> {
            self.calls.lock().unwrap().push(tool_name.to_string());
            Ok("{\"ok\":true}".to_string())
        }
    }

    #[derive(Clone)]
    struct ScriptedToolLlm {
        tool: String,
        args: String,
        calls: Arc<Mutex<usize>>,
    }

    #[async_trait]
    impl ChatCompletion for ScriptedToolLlm {
        async fn complete(
            &self,
            _request: &ChatCompletionRequest,
        ) -> Result<ChatCompletionResponse, LanguageModelError> {
            let mut n = self.calls.lock().unwrap();
            *n += 1;
            if *n == 1 {
                let tool_call = ToolCall::builder()
                    .id("call-1")
                    .name(self.tool.clone())
                    .args(self.args.clone())
                    .build()
                    .expect("scripted tool call should build");
                Ok(ChatCompletionResponse::builder()
                    .tool_calls(vec![tool_call])
                    .build()
                    .expect("scripted tool response should build"))
            } else {
                Ok(ChatCompletionResponse::builder()
                    .message("final answer".to_string())
                    .build()
                    .expect("scripted final response should build"))
            }
        }
    }

    fn recording_permission_callback(
        yolo: bool,
        allow_tools: bool,
        log: Arc<Mutex<Vec<(String, bool)>>>,
    ) -> Box<
        dyn Fn(
                maho_agent::PermissionRequest,
            )
                -> std::pin::Pin<Box<dyn std::future::Future<Output = PermissionDecision> + Send>>
            + Send
            + Sync,
    > {
        Box::new(move |req| {
            let is_mcp = req.tool_name.starts_with("mcp:");
            let allow = yolo || (is_mcp && allow_tools);
            log.lock().unwrap().push((req.tool_name.clone(), allow));
            Box::pin(async move {
                if allow {
                    PermissionDecision::Allow
                } else {
                    PermissionDecision::Deny
                }
            })
        })
    }

    fn make_backend(bridge: Option<RecordingBridge>) -> SwiftideBackend {
        let backend = SwiftideBackend::new(
            Arc::new(MockStorage::default()) as Arc<dyn AgentStorage>,
            None,
            std::env::temp_dir(),
            true,
        );
        if let Some(b) = bridge {
            backend
                .set_browser_tool_bridge(Arc::new(b))
                .expect("set browser bridge");
        }
        backend
    }

    #[tokio::test(flavor = "multi_thread")]
    async fn run_invokes_registered_tool() {
        let bridge = RecordingBridge::default();
        let calls = Arc::clone(&bridge.calls);
        let backend = make_backend(Some(bridge));
        backend.set_llm_override(Box::new(ScriptedToolLlm {
            tool: "read_current_page".to_string(),
            args: "{}".to_string(),
            calls: Arc::new(Mutex::new(0)),
        }));
        backend.set_approval_policy("allow".to_string());

        let result = tokio::time::timeout(Duration::from_secs(30), backend.run_prompt("go"))
            .await
            .expect("run_prompt must not hang");
        result.expect("run_prompt should succeed");

        let invoked = calls.lock().unwrap().clone();
        assert!(
            invoked.iter().any(|n| n == "read_current_page"),
            "the agent loop must actually invoke the registered browser tool; invoked={invoked:?}"
        );
    }

    #[tokio::test(flavor = "multi_thread")]
    async fn run_deny_sensitive_no_deadlock() {
        let log = Arc::new(Mutex::new(Vec::<(String, bool)>::new()));
        let backend = make_backend(None);
        backend.set_llm_override(Box::new(ScriptedToolLlm {
            tool: "shell_exec".to_string(),
            args: "{\"command\": \"git status\"}".to_string(),
            calls: Arc::new(Mutex::new(0)),
        }));
        backend.set_approval_policy("deny-sensitive".to_string());
        backend
            .set_permission_callback(recording_permission_callback(
                false,
                false,
                Arc::clone(&log),
            ))
            .expect("set permission callback");

        let result = tokio::time::timeout(Duration::from_secs(30), backend.run_prompt("go"))
            .await
            .expect("run_prompt must terminate without deadlock when a sensitive tool is denied");
        result.expect("run_prompt should return normally after denial");

        let decisions = log.lock().unwrap().clone();
        assert!(
            decisions
                .iter()
                .any(|(name, allow)| name == "shell_exec" && !allow),
            "the sensitive tool must be gated and denied; decisions={decisions:?}"
        );
    }

    #[tokio::test(flavor = "multi_thread")]
    async fn run_yolo_bypasses() {
        let log = Arc::new(Mutex::new(Vec::<(String, bool)>::new()));
        let backend = make_backend(None);
        backend.set_llm_override(Box::new(ScriptedToolLlm {
            tool: "shell_exec".to_string(),
            args: "{\"command\": \"git status\"}".to_string(),
            calls: Arc::new(Mutex::new(0)),
        }));
        backend.set_approval_policy("allow".to_string());
        backend
            .set_permission_callback(recording_permission_callback(true, false, Arc::clone(&log)))
            .expect("set permission callback");

        let result = tokio::time::timeout(Duration::from_secs(30), backend.run_prompt("go"))
            .await
            .expect("run_prompt must not hang under --yolo");
        result.expect("run_prompt should succeed under --yolo");

        let decisions = log.lock().unwrap().clone();
        assert!(
            decisions
                .iter()
                .any(|(name, allow)| name == "shell_exec" && *allow),
            "--yolo must allow the sensitive tool; decisions={decisions:?}"
        );
    }
}
