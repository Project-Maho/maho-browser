use std::sync::{Arc, Mutex, MutexGuard};

use async_trait::async_trait;
use maho_agent::{
    AgentError, AgentRuntime, AgentStorage, BrowserToolBridge, SwiftideBackend, ZeroizedString,
};
use maho_types::chat::ChatMessage;
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

impl BrowserToolBridge for RecordingBrowserBridge {
    fn invoke(&self, tool_name: &str, args_json: &str) -> Result<String, String> {
        push_locked(&self.calls, (tool_name.to_string(), args_json.to_string()));
        Ok(r#"{"ok":true,"scrolled":true}"#.to_string())
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

        Ok(ChatCompletionResponse::builder()
            .message("done".to_string())
            .build()
            .expect("test assistant response builds"))
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
