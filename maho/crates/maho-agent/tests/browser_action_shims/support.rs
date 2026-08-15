use std::sync::{Arc, Mutex, MutexGuard};

use async_trait::async_trait;
use maho_agent::{
    AgentError, AgentRuntime, AgentStorage, BrowserToolBridge, BrowserToolBridgeError,
    BrowserToolDescriptor, BrowserToolExecution, BrowserToolExecutionReceipt, BrowserToolPolicy,
    SwiftideBackend, ZeroizedString,
};
use maho_types::chat::ChatMessage;
use maho_types::tool::ToolPermission;
use swiftide_core::chat_completion::{
    errors::LanguageModelError, ChatCompletion, ChatCompletionRequest, ChatCompletionResponse,
    ToolCall,
};

pub struct BrowserActionHarness {
    pub backend: SwiftideBackend,
    _temp_dir: tempfile::TempDir,
}

#[derive(Clone)]
struct BrowserActionStorage {
    root: String,
    profile_tools: Vec<String>,
}

#[async_trait]
impl AgentStorage for BrowserActionStorage {
    async fn get_byok_key(&self, _provider: &str) -> Result<Option<ZeroizedString>, AgentError> {
        Ok(Some(ZeroizedString::new("mock-key".to_string())))
    }

    async fn get_setting(&self, _key: &str) -> Result<Option<ZeroizedString>, AgentError> {
        Ok(None)
    }

    async fn save_message(&self, _: &str, _: &str, _: &str) -> Result<(), AgentError> {
        Ok(())
    }

    async fn get_history(&self, _session_id: &str) -> Result<Vec<ChatMessage>, AgentError> {
        Ok(Vec::new())
    }

    async fn list_workspaces(&self) -> Result<Vec<maho_types::ai::AiWorkspace>, AgentError> {
        Ok(vec![maho_types::ai::AiWorkspace {
            id: "workspace-browser-actions".to_string(),
            name: "Browser Actions".to_string(),
            profile_id: Some("profile-browser-actions".to_string()),
            space_id: None,
            workspace_root: Some(self.root.clone()),
            created_at: String::new(),
            updated_at: String::new(),
        }])
    }

    async fn get_profile(&self, id: &str) -> Result<Option<maho_types::ai::AiProfile>, AgentError> {
        if id != "profile-browser-actions" {
            return Ok(None);
        }

        Ok(Some(maho_types::ai::AiProfile {
            id: id.to_string(),
            name: "Browser Actions".to_string(),
            system_prompt: String::new(),
            preferred_model: None,
            tools: self.profile_tools.clone(),
            mcp_servers: vec![],
            is_default: true,
            created_at: String::new(),
            updated_at: String::new(),
        }))
    }
}

#[derive(Default)]
pub struct RecordingBrowserBridge {
    calls: Mutex<Vec<(String, String)>>,
}

impl RecordingBrowserBridge {
    pub fn calls(&self) -> Vec<(String, String)> {
        clone_locked(&self.calls)
    }
}

#[async_trait]
impl BrowserToolBridge for RecordingBrowserBridge {
    async fn list_tool_descriptors(
        &self,
    ) -> Result<Vec<BrowserToolDescriptor>, BrowserToolBridgeError> {
        let names = [
            "list_browser_tabs",
            "read_current_page",
            "browser_accessibility_snapshot",
            "browser_navigate",
            "browser_click",
            "browser_type",
            "browser_select",
            "browser_scroll",
            "browser_hover",
            "browser_key_press",
        ];
        Ok(names
            .into_iter()
            .map(|name| BrowserToolDescriptor {
                capability_id: name.to_string(),
                name: name.to_string(),
                description: format!("Test browser tool {name}"),
                input_schema: serde_json::json!({"type":"object","properties":{}}),
                schema_version: 1,
                policy: BrowserToolPolicy {
                    sensitive: name.starts_with("browser_")
                        && name != "browser_accessibility_snapshot",
                    permission: ToolPermission::AutoApprove,
                },
            })
            .collect())
    }

    async fn execute_tool(
        &self,
        capability_id: &str,
        args_json: &str,
    ) -> Result<BrowserToolExecution, BrowserToolBridgeError> {
        push_locked(
            &self.calls,
            (capability_id.to_string(), args_json.to_string()),
        );
        let output_json = if capability_id == "browser_tab_list" {
            r#"{"tabs":[{"id":1,"is_active":true,"targetable":true}]}"#.to_string()
        } else {
            r#"{"ok":true,"scrolled":true}"#.to_string()
        };
        Ok(BrowserToolExecution {
            output_json,
            receipt: BrowserToolExecutionReceipt {
                capability_id: capability_id.to_string(),
                execution_id: "test-execution".to_string(),
                metadata: serde_json::json!({}),
            },
        })
    }
}

#[derive(Clone)]
pub struct OneToolCallLlm {
    tool_name: String,
    args: String,
    completions: Arc<Mutex<usize>>,
}

impl OneToolCallLlm {
    pub fn new(tool_name: &str, args: &str) -> Self {
        Self {
            tool_name: tool_name.to_string(),
            args: args.to_string(),
            completions: Arc::new(Mutex::new(0)),
        }
    }
}

#[async_trait]
impl ChatCompletion for OneToolCallLlm {
    async fn complete(
        &self,
        _request: &ChatCompletionRequest,
    ) -> Result<ChatCompletionResponse, LanguageModelError> {
        let mut completions = lock(&self.completions);
        *completions += 1;
        if *completions == 1 {
            let tool_call = ToolCall::builder()
                .id("call-browser-action".to_string())
                .name(self.tool_name.clone())
                .args(self.args.clone())
                .build()
                .expect("test tool call builds");
            return Ok(ChatCompletionResponse::builder()
                .tool_calls(vec![tool_call])
                .build()
                .expect("test tool-call response builds"));
        }

        // Build the final answer so it both carries the accumulated message
        // and emits a streaming delta, mirroring a real streaming LLM. The
        // tool-call turn above emits no message and therefore streams nothing,
        // so only the authoritative final answer reaches the token callback.
        let mut response = ChatCompletionResponse::builder()
            .build()
            .expect("test assistant response builds");
        response.append_message_delta(Some("done"));
        Ok(response)
    }
}

pub fn backend_with_profile_tools(profile_tools: &[&str]) -> BrowserActionHarness {
    let temp_dir = tempfile::tempdir().expect("tempdir creates");
    let storage = Arc::new(BrowserActionStorage {
        root: temp_dir.path().to_string_lossy().to_string(),
        profile_tools: profile_tools
            .iter()
            .map(|tool| (*tool).to_string())
            .collect(),
    });
    let backend = SwiftideBackend::new(
        storage as Arc<dyn AgentStorage>,
        None,
        temp_dir.path().to_path_buf(),
        true,
    );
    BrowserActionHarness {
        backend,
        _temp_dir: temp_dir,
    }
}

pub async fn tool_names(backend: &SwiftideBackend) -> Vec<String> {
    let mut names: Vec<String> = backend
        .list_tools()
        .await
        .expect("list_tools succeeds")
        .into_iter()
        .map(|tool| tool.name)
        .collect();
    names.sort();
    names
}

pub fn has_tool(names: &[String], name: &str) -> bool {
    names.iter().any(|tool_name| tool_name == name)
}

pub fn install_bridge(backend: &SwiftideBackend) -> Arc<RecordingBrowserBridge> {
    let bridge = Arc::new(RecordingBrowserBridge::default());
    backend
        .set_browser_tool_bridge(Arc::clone(&bridge) as Arc<dyn BrowserToolBridge>)
        .expect("bridge installs");
    bridge
}

pub fn lock<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T> {
    mutex
        .lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner)
}

pub fn clone_locked<T: Clone>(mutex: &Mutex<T>) -> T {
    lock(mutex).clone()
}

pub fn push_locked<T>(mutex: &Mutex<Vec<T>>, value: T) {
    lock(mutex).push(value);
}

pub fn increment_locked(mutex: &Mutex<usize>) {
    *lock(mutex) += 1;
}
