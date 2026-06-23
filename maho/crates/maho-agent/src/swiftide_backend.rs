use std::borrow::Cow;
use std::collections::HashSet;
use std::sync::{Arc, Mutex, MutexGuard};
use async_trait::async_trait;
use maho_types::chat::{ChatMessage as MahoChatMessage, ChatContent};
use swiftide_core::{
    AgentContext, Tool,
    chat_completion::{ChatMessage as SwiftideChatMessage, ToolCall, ToolOutput, ToolSpec},
};

use crate::{
    AgentRuntime, AgentStorage, ToolDefinition, AgentError, PermissionCallback,
    PermissionDecision, PermissionRequest,
};

const FS_READ_MAX_BYTES: u64 = 1024 * 1024;
const OPENAI_DEFAULT_API_BASE: &str = "https://api.openai.com/v1";

trait MutexExt<T> {
    fn lock_unpoison(&self) -> MutexGuard<'_, T>;
}

impl<T> MutexExt<T> for Mutex<T> {
    fn lock_unpoison(&self) -> MutexGuard<'_, T> {
        self.lock().unwrap_or_else(|e| {
            tracing::error!(
                target = "maho_agent::mutex",
                mutex_type = std::any::type_name::<T>(),
                "Mutex poisoned by prior panic; recovering with possibly inconsistent state"
            );
            e.into_inner()
        })
    }
}

#[derive(Clone)]
struct MahoOpenAIConfig {
    // SAFETY: contains BYOK plaintext, exclude from crash dumps
    api_key: Arc<secrecy::SecretString>,
    api_base: String,
}

impl Default for MahoOpenAIConfig {
    fn default() -> Self {
        // Required by GenericOpenAIBuilder bound only; never reaches HTTP at runtime
        // because callers override the config before `.client(...)`. Empty "Bearer " on the wire = bug.
        Self {
            api_key: Arc::new(secrecy::SecretString::from(String::new())),
            api_base: OPENAI_DEFAULT_API_BASE.to_string(),
        }
    }
}

impl std::fmt::Debug for MahoOpenAIConfig {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("MahoOpenAIConfig")
            .field("api_key", &"[REDACTED]")
            .field("api_base", &self.api_base)
            .finish()
    }
}

impl async_openai::config::Config for MahoOpenAIConfig {
    fn headers(&self) -> reqwest::header::HeaderMap {
        use secrecy::ExposeSecret;
        let mut map = reqwest::header::HeaderMap::new();
        let mut bearer = zeroize::Zeroizing::new(String::with_capacity(
            "Bearer ".len() + self.api_key.expose_secret().len(),
        ));
        bearer.push_str("Bearer ");
        bearer.push_str(self.api_key.expose_secret());
        match reqwest::header::HeaderValue::from_str(&bearer) {
            Ok(v) => {
                map.insert(reqwest::header::AUTHORIZATION, v);
            }
            Err(_) => {
                tracing::error!(
                    target = "maho_agent::config",
                    "BYOK API key produced invalid Authorization header bytes; \
                     request will be sent without auth and will likely receive 401"
                );
            }
        }
        map
    }

    fn url(&self, path: &str) -> String {
        format!("{}{}", self.api_base, path)
    }

    fn query(&self) -> Vec<(&str, &str)> {
        vec![]
    }

    fn api_base(&self) -> &str {
        &self.api_base
    }

    fn api_key(&self) -> &secrecy::SecretString {
        &self.api_key
    }
}

#[derive(Clone)]
struct PermissionGatedTool {
    inner_tool: Arc<dyn Tool>,
    permission_callback: Arc<Mutex<Option<PermissionCallback>>>,
    denied_flag: Arc<Mutex<bool>>,
    session_id: String,
}

#[async_trait]
impl Tool for PermissionGatedTool {
    async fn invoke(
        &self,
        agent_context: &dyn AgentContext,
        tool_call: &ToolCall,
    ) -> anyhow::Result<ToolOutput, swiftide_core::chat_completion::errors::ToolError> {
        if *self.denied_flag.lock_unpoison() {
            return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                anyhow::anyhow!("Permission Denied"),
            ));
        }

        let decision_fut = {
            let callback_opt = self.permission_callback.lock_unpoison();
            if let Some(ref callback) = *callback_opt {
                let request = PermissionRequest {
                    tool_name: self.name().to_string(),
                    arguments: tool_call.args().unwrap_or("{}").to_string(),
                };
                Some(callback(request))
            } else {
                None
            }
        };

        let decision = if let Some(fut) = decision_fut {
            fut.await
        } else {
            PermissionDecision::Deny
        };

        if decision == PermissionDecision::Deny {
            tracing::warn!(
                tool = %self.name(),
                "Tool invocation denied by permission callback; session denied_flag set",
            );
            *self.denied_flag.lock_unpoison() = true;
            return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                anyhow::anyhow!("Permission Denied"),
            ));
        }

        if decision == PermissionDecision::Allow {
            tracing::info!(
                target = "maho_agent::audit",
                tool = %self.name(),
                session_id = %self.session_id,
                "Tool invocation allowed; proceeding",
            );
        }

        self.inner_tool.invoke(agent_context, tool_call).await
    }

    fn name(&self) -> Cow<'_, str> {
        self.inner_tool.name()
    }

    fn tool_spec(&self) -> ToolSpec {
        self.inner_tool.tool_spec()
    }
}

// Manually implement some basic tools
#[derive(Clone)]
struct FsReadTool {
    workspace_root: std::path::PathBuf,
}

#[async_trait]
impl Tool for FsReadTool {
    async fn invoke(
        &self,
        _agent_context: &dyn AgentContext,
        tool_call: &ToolCall,
    ) -> anyhow::Result<ToolOutput, swiftide_core::chat_completion::errors::ToolError> {
        let args: serde_json::Value = serde_json::from_str(tool_call.args().unwrap_or("{}"))
            .map_err(swiftide_core::chat_completion::errors::ToolError::unknown)?;
        let path_str = args.get("path").and_then(|v| v.as_str())
            .ok_or_else(|| swiftide_core::chat_completion::errors::ToolError::unknown(anyhow::anyhow!("Missing 'path' parameter")))?;

        let target_path = std::path::Path::new(path_str);
        let target_path_absolute = if target_path.is_absolute() {
            target_path.to_path_buf()
        } else {
            self.workspace_root.join(target_path)
        };

        let canonical_workspace = self.workspace_root.canonicalize()
            .map_err(|e| swiftide_core::chat_completion::errors::ToolError::unknown(anyhow::anyhow!("Workspace error: {e}")))?;

        let canonical_target = match target_path_absolute.canonicalize() {
            Ok(p) => p,
            Err(e) => return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                anyhow::anyhow!("Invalid path or file does not exist: {e}")
            )),
        };

        if !canonical_target.starts_with(&canonical_workspace) {
            return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                anyhow::anyhow!("Security violation: Access denied to path outside workspace")
            ));
        }

        let metadata = tokio::fs::metadata(&canonical_target).await
            .map_err(swiftide_core::chat_completion::errors::ToolError::unknown)?;
        if metadata.len() > FS_READ_MAX_BYTES {
            return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                anyhow::anyhow!("File too large: {} bytes (max {} bytes)", metadata.len(), FS_READ_MAX_BYTES)
            ));
        }

        let content = tokio::fs::read_to_string(canonical_target).await
            .map_err(swiftide_core::chat_completion::errors::ToolError::unknown)?;
        
        Ok(ToolOutput::Text(content))
    }

    fn name(&self) -> Cow<'_, str> {
        Cow::Borrowed("fs_read")
    }

    fn tool_spec(&self) -> ToolSpec {
        ToolSpec {
            name: "fs_read".to_string(),
            description: "Read a file from the filesystem".to_string(),
            parameters_schema: Some(serde_json::from_value(serde_json::json!({
                "type": "object",
                "properties": {
                    "path": { "type": "string" }
                },
                "required": ["path"]
            })).unwrap()),
        }
    }
}

#[derive(Clone)]
struct ShellExecTool {
    workspace_root: std::path::PathBuf,
}

#[async_trait]
impl Tool for ShellExecTool {
    async fn invoke(
        &self,
        _agent_context: &dyn AgentContext,
        tool_call: &ToolCall,
    ) -> anyhow::Result<ToolOutput, swiftide_core::chat_completion::errors::ToolError> {
        let args: serde_json::Value = serde_json::from_str(tool_call.args().unwrap_or("{}"))
            .map_err(swiftide_core::chat_completion::errors::ToolError::unknown)?;
        let command = args.get("command").and_then(|v| v.as_str())
            .ok_or_else(|| swiftide_core::chat_completion::errors::ToolError::unknown(anyhow::anyhow!("Missing 'command' parameter")))?;
        
        // 1. Safety check: reject shell metacharacters
        let forbidden_chars = ['|', ';', '&', '$', '`', '>', '<', '(', ')', '*', '?', '\\'];
        if command.chars().any(|c| forbidden_chars.contains(&c)) {
            return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                anyhow::anyhow!("Security violation: Shell metacharacters are not allowed")
            ));
        }

        // 2. Parse command into arguments
        let parts: Vec<&str> = command.split_whitespace().collect();
        if parts.is_empty() {
            return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                anyhow::anyhow!("Empty command")
            ));
        }
        let exe = parts[0];

        // Strict validation: drop cargo, python3, bun. Only allow git and lsof.
        if exe != "git" && exe != "lsof" {
            return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                anyhow::anyhow!(format!("Security violation: Command '{exe}' is not allowed"))
            ));
        }

        if exe == "git" {
            if parts.len() < 2 {
                return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                    anyhow::anyhow!("Security violation: git subcommand required")
                ));
            }
            let sub = parts[1];
            let allowed_subs = ["status", "diff", "log", "rev-parse"];
            if !allowed_subs.contains(&sub) {
                return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                    anyhow::anyhow!(format!("Security violation: git subcommand '{sub}' is not allowed"))
                ));
            }
            if sub == "diff" {
                if parts.len() == 2 {
                    return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                        anyhow::anyhow!("Security violation: bare 'git diff' is not allowed (would expose uncommitted working-tree content)")
                    ));
                }
                for arg in &parts[2..] {
                    if !arg.starts_with('-') {
                        return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                            anyhow::anyhow!("Security violation: git diff with positional refs is not allowed (would enable committed-content exfiltration)")
                        ));
                    }
                }
            }
            for arg in &parts[2..] {
                if arg.starts_with('-') {
                    let allowed_flags = ["-n", "--oneline", "--show-toplevel"];
                    if !allowed_flags.contains(arg) && !arg.chars().skip(1).all(|c| c.is_ascii_digit()) {
                        return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                            anyhow::anyhow!(format!("Security violation: git argument '{arg}' is not allowed"))
                        ));
                    }
                } else {
                    if !arg.chars().all(|c| c.is_ascii_alphanumeric() || c == '.' || c == '_' || c == '-' || c == '/') {
                        return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                            anyhow::anyhow!(format!("Security violation: git argument '{arg}' contains invalid characters"))
                        ));
                    }
                }
            }
        } else if exe == "lsof" {
            if parts.len() != 2 {
                return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                    anyhow::anyhow!("Security violation: lsof must be run with exactly one argument")
                ));
            }
            let arg = parts[1];
            let lock_path_1 = format!("{}/.build.lock", self.workspace_root.display());
            if arg != ".build.lock" && arg != lock_path_1 {
                return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                    anyhow::anyhow!("Security violation: lsof is only allowed on the workspace lock file")
                ));
            }
        }

        // 3. Scrub environment (only preserve PATH)
        let path_env = std::env::var("PATH").unwrap_or_default();

        // 4. Execute command with a timeout (e.g., 5 seconds) and cap output
        let mut cmd = tokio::process::Command::new(exe);
        cmd.args(&parts[1..])
            .current_dir(&self.workspace_root)
            .env_clear()
            .env("PATH", path_env);

        let output_fut = cmd.output();
        let output = match tokio::time::timeout(std::time::Duration::from_secs(5), output_fut).await {
            Ok(Ok(out)) => out,
            Ok(Err(e)) => return Err(swiftide_core::chat_completion::errors::ToolError::unknown(e)),
            Err(_) => return Err(swiftide_core::chat_completion::errors::ToolError::unknown(
                anyhow::anyhow!("Command execution timed out after 5 seconds")
            )),
        };

        let mut result = String::from_utf8_lossy(&output.stdout).into_owned()
            + &String::from_utf8_lossy(&output.stderr).into_owned();

        // 5. Cap output to 10k characters to prevent self-exfiltration of huge logs/files
        if result.len() > 10000 {
            result.truncate(10000);
            result.push_str("\n... [Output truncated to 10,000 characters]");
        }

        Ok(ToolOutput::Text(result))
    }

    fn name(&self) -> Cow<'_, str> {
        Cow::Borrowed("shell_exec")
    }

    fn tool_spec(&self) -> ToolSpec {
        ToolSpec {
            name: "shell_exec".to_string(),
            description: "Execute a command in the shell".to_string(),
            parameters_schema: Some(serde_json::from_value(serde_json::json!({
                "type": "object",
                "properties": {
                    "command": { "type": "string" }
                },
                "required": ["command"]
            })).unwrap()),
        }
    }
}

#[derive(Clone)]
struct CameraTool;

#[async_trait]
impl Tool for CameraTool {
    async fn invoke(
        &self,
        _agent_context: &dyn AgentContext,
        _tool_call: &ToolCall,
    ) -> anyhow::Result<ToolOutput, swiftide_core::chat_completion::errors::ToolError> {
        Ok(ToolOutput::Text("Picture captured successfully. [Mock Image Data]".to_string()))
    }

    fn name(&self) -> Cow<'_, str> {
        Cow::Borrowed("camera")
    }

    fn tool_spec(&self) -> ToolSpec {
        ToolSpec {
            name: "camera".to_string(),
            description: "Capture a photo using the mobile camera".to_string(),
            parameters_schema: Some(serde_json::from_value(serde_json::json!({
                "type": "object",
                "properties": {}
            })).unwrap()),
        }
    }
}

#[derive(Clone)]
struct PhotoLibraryTool;

#[async_trait]
impl Tool for PhotoLibraryTool {
    async fn invoke(
        &self,
        _agent_context: &dyn AgentContext,
        _tool_call: &ToolCall,
    ) -> anyhow::Result<ToolOutput, swiftide_core::chat_completion::errors::ToolError> {
        Ok(ToolOutput::Text("Photo selected from mobile library. [Mock Photo]".to_string()))
    }

    fn name(&self) -> Cow<'_, str> {
        Cow::Borrowed("photo_library")
    }

    fn tool_spec(&self) -> ToolSpec {
        ToolSpec {
            name: "photo_library".to_string(),
            description: "Select a photo from the mobile photo library".to_string(),
            parameters_schema: Some(serde_json::from_value(serde_json::json!({
                "type": "object",
                "properties": {}
            })).unwrap()),
        }
    }
}

#[derive(Clone)]
struct ClipboardTool;

#[async_trait]
impl Tool for ClipboardTool {
    async fn invoke(
        &self,
        _agent_context: &dyn AgentContext,
        tool_call: &ToolCall,
    ) -> anyhow::Result<ToolOutput, swiftide_core::chat_completion::errors::ToolError> {
        let args: serde_json::Value = serde_json::from_str(tool_call.args().unwrap_or("{}"))
            .map_err(swiftide_core::chat_completion::errors::ToolError::unknown)?;
        let action = args.get("action").and_then(|v| v.as_str())
            .ok_or_else(|| swiftide_core::chat_completion::errors::ToolError::unknown(anyhow::anyhow!("Missing 'action' parameter")))?;
        
        if action == "write" {
            let text = args.get("text").and_then(|v: &serde_json::Value| v.as_str()).unwrap_or("");
            Ok(ToolOutput::Text(format!("Copied to clipboard: {text}")))
        } else {
            Ok(ToolOutput::Text("Content read from clipboard: [Mock Clipboard Content]".to_string()))
        }
    }

    fn name(&self) -> Cow<'_, str> {
        Cow::Borrowed("clipboard")
    }

    fn tool_spec(&self) -> ToolSpec {
        ToolSpec {
            name: "clipboard".to_string(),
            description: "Read or write to the system clipboard".to_string(),
            parameters_schema: Some(serde_json::from_value(serde_json::json!({
                "type": "object",
                "properties": {
                    "action": { "type": "string", "enum": ["read", "write"] },
                    "text": { "type": "string" }
                },
                "required": ["action"]
            })).unwrap()),
        }
    }
}

use crate::ZeroizedString;

pub struct SwiftideBackend {
    storage: Arc<dyn AgentStorage>,
    permission_callback: Arc<Mutex<Option<PermissionCallback>>>,
    active_sessions: Arc<Mutex<std::collections::HashMap<String, tokio::task::AbortHandle>>>,
    llm_override: Arc<Mutex<Option<Box<dyn swiftide_core::chat_completion::ChatCompletion>>>>,
    secure_storage_callback: Arc<Mutex<Option<Box<dyn Fn(&str) -> Result<ZeroizedString, AgentError> + Send + Sync + 'static>>>>,
    workspace_root: std::path::PathBuf,
    allow_insecure_key_storage: bool,
}

impl Clone for SwiftideBackend {
    fn clone(&self) -> Self {
        Self {
            storage: Arc::clone(&self.storage),
            permission_callback: Arc::clone(&self.permission_callback),
            active_sessions: Arc::clone(&self.active_sessions),
            llm_override: Arc::clone(&self.llm_override),
            secure_storage_callback: Arc::clone(&self.secure_storage_callback),
            workspace_root: self.workspace_root.clone(),
            allow_insecure_key_storage: self.allow_insecure_key_storage,
        }
    }
}

impl SwiftideBackend {
    pub fn new(
        storage: Arc<dyn AgentStorage>,
        permission_callback: Option<PermissionCallback>,
        workspace_root: std::path::PathBuf,
        allow_insecure_key_storage: bool,
    ) -> Self {
        crate::init_audit_logging();
        Self {
            storage,
            permission_callback: Arc::new(Mutex::new(permission_callback)),
            active_sessions: Arc::new(Mutex::new(std::collections::HashMap::new())),
            llm_override: Arc::new(Mutex::new(None)),
            secure_storage_callback: Arc::new(Mutex::new(None)),
            workspace_root,
            allow_insecure_key_storage,
        }
    }

    pub fn set_llm_override(&self, llm: Box<dyn swiftide_core::chat_completion::ChatCompletion>) {
        *self.llm_override.lock_unpoison() = Some(llm);
    }

    fn build_gated_tool(&self, tool: Arc<dyn Tool>, denied_flag: Arc<Mutex<bool>>, session_id: &str) -> Box<dyn Tool> {
        Box::new(PermissionGatedTool {
            inner_tool: tool,
            permission_callback: Arc::clone(&self.permission_callback),
            denied_flag,
            session_id: session_id.to_string(),
        }) as Box<dyn Tool>
    }

    async fn run_turn_internal(
        &self,
        session_id: &str,
        message: MahoChatMessage,
        denied_flag: Arc<Mutex<bool>>,
        on_token: Option<Box<dyn for<'a> Fn(&'a str) + Send + Sync + 'static>>,
    ) -> Result<MahoChatMessage, AgentError> {
        let content_str = match &message.content {
            ChatContent::Text(t) => t.clone(),
            ChatContent::Image { .. } => {
                return Err(AgentError::ExecutionError("Multimodal image content is not supported yet".to_string()));
            }
            ChatContent::Mixed(_) => {
                return Err(AgentError::ExecutionError("Multimodal mixed content is not supported yet".to_string()));
            }
        };

        // Save user message to storage
        self.storage.save_message(session_id, &message.role, &content_str).await?;

        // Determine LLM client
        let llm_override_opt = {
            let lock = self.llm_override.lock_unpoison();
            lock.as_ref().map(|llm| dyn_clone::clone_box(&**llm))
        };

        let secure_cb_opt = {
            let secure_cb = self.secure_storage_callback.lock_unpoison();
            if let Some(ref cb) = *secure_cb {
                if let Ok(key) = cb("openai") {
                    Some(("openai", key))
                } else if let Ok(key) = cb("anthropic") {
                    Some(("anthropic", key))
                } else {
                    None
                }
            } else {
                None
            }
        };

        let llm: Box<dyn swiftide_core::chat_completion::ChatCompletion> = if let Some(llm) = llm_override_opt {
            llm
        } else {
            let (provider, api_key) = if let Some(res) = secure_cb_opt {
                res
            } else {
                if !self.allow_insecure_key_storage {
                    return Err(AgentError::ExecutionError("Secure storage callback is missing or returned no keys, and insecure key storage is not allowed".to_string()));
                }
                if let Some(key) = self.storage.get_byok_key("openai").await? {
                    ("openai", key)
                } else if let Some(key) = self.storage.get_byok_key("anthropic").await? {
                    ("anthropic", key)
                } else {
                    return Err(AgentError::ExecutionError("No BYOK API key configured".to_string()));
                }
            };

            // Instantiate LLM client
            if provider == "openai" {
                let config = MahoOpenAIConfig {
                    api_key: Arc::new(api_key.secret().clone()),
                    api_base: OPENAI_DEFAULT_API_BASE.to_string(),
                };
                let client = swiftide_integrations::openai::GenericOpenAIBuilder::<MahoOpenAIConfig>::default()
                    .default_prompt_model("gpt-4o")
                    .client(async_openai::Client::with_config(config))
                    .build()
                    .map_err(|e| AgentError::ExecutionError(format!("Failed to build OpenAI client: {e}")))?;
                Box::new(client)
            } else {
                let client = async_anthropic::Client::from_api_key(api_key.secret().clone());
                let swiftide_anthropic = swiftide_integrations::anthropic::Anthropic::builder()
                    .default_prompt_model("claude-3-5-sonnet")
                    .client(client)
                    .build()
                    .map_err(|e| AgentError::ExecutionError(format!("Failed to build Anthropic client: {e}")))?;
                Box::new(swiftide_anthropic)
            }
        };

        // Get history
        let history = self.storage.get_history(session_id).await?;
        let mut swiftide_history = Vec::new();
        for msg in history {
            let text_content = match &msg.content {
                ChatContent::Text(t) => t.clone(),
                ChatContent::Image { .. } => {
                    return Err(AgentError::ExecutionError("Multimodal image content is not supported in history".to_string()));
                }
                ChatContent::Mixed(_) => {
                    return Err(AgentError::ExecutionError("Multimodal mixed content is not supported in history".to_string()));
                }
            };
            let swiftide_msg = match msg.role.as_str() {
                "system" => SwiftideChatMessage::new_system(text_content),
                "user" => SwiftideChatMessage::new_user(text_content),
                "assistant" => SwiftideChatMessage::new_assistant(Some(text_content), None),
                _ => SwiftideChatMessage::new_user(text_content),
            };
            swiftide_history.push(swiftide_msg);
        }

        // Construct platform-gated tools
        let mut platform_tools = Vec::new();
        #[cfg(any(target_os = "ios", target_os = "android"))]
        {
            platform_tools.push(self.build_gated_tool(Arc::new(CameraTool), denied_flag.clone(), session_id));
            platform_tools.push(self.build_gated_tool(Arc::new(PhotoLibraryTool), denied_flag.clone(), session_id));
            platform_tools.push(self.build_gated_tool(Arc::new(ClipboardTool), denied_flag.clone(), session_id));
        }
        #[cfg(not(any(target_os = "ios", target_os = "android")))]
        {
            platform_tools.push(self.build_gated_tool(Arc::new(FsReadTool { workspace_root: self.workspace_root.clone() }), denied_flag.clone(), session_id));
            platform_tools.push(self.build_gated_tool(Arc::new(ShellExecTool { workspace_root: self.workspace_root.clone() }), denied_flag.clone(), session_id));
        }

        // Load DefaultContext with history
        let mut context = swiftide_agents::DefaultContext::default();
        context.with_existing_messages(swiftide_history).await
            .map_err(|e| AgentError::ExecutionError(e.to_string()))?;

        // Instantiate Agent Builder
        let mut builder = swiftide_agents::Agent::builder();
        builder.llm(&llm)
            .context(context)
            .tools(platform_tools);

        if let Some(on_token_cb) = on_token {
            let on_token_cb = Arc::new(on_token_cb);
            builder.on_stream(move |_agent, response| {
                let cb = Arc::clone(&on_token_cb);
                if let Some(ref delta) = response.delta {
                    if let Some(ref chunk) = delta.message_chunk {
                        cb(chunk);
                    }
                }
                Box::pin(async move { Ok(()) })
            });
        }

        let mut agent = builder.build()
            .map_err(|e| AgentError::ExecutionError(e.to_string()))?;

        // Run turn with 30s timeout
        tokio::time::timeout(std::time::Duration::from_secs(30), agent.query(content_str.clone()))
            .await
            .map_err(|_| AgentError::ExecutionError("Agent query timed out".to_string()))?
            .map_err(|e| AgentError::ExecutionError(e.to_string()))?;

        // Check denied flag
        if *denied_flag.lock_unpoison() {
            return Err(AgentError::PermissionDenied);
        }

        // Retrieve the last message from the context
        let result_history = agent.context().history().await
            .map_err(|e| AgentError::ExecutionError(e.to_string()))?;

        if let Some(SwiftideChatMessage::Assistant(text, _)) = result_history.last() {
            let res_text = text.clone().unwrap_or_default();
            // Save response to storage
            self.storage.save_message(session_id, "assistant", &res_text).await?;
            Ok(MahoChatMessage::assistant(ChatContent::text(res_text)))
        } else {
            Err(AgentError::ExecutionError("No assistant response generated".to_string()))
        }
    }
}

#[async_trait]
impl AgentRuntime for SwiftideBackend {
    async fn run_turn(
        &self,
        session_id: &str,
        message: MahoChatMessage,
        on_token: Option<Box<dyn for<'a> Fn(&'a str) + Send + Sync + 'static>>,
    ) -> Result<MahoChatMessage, AgentError> {
        let start_time = std::time::Instant::now();
        tracing::info!(
            target = "maho_agent::audit",
            session_id = %session_id,
            event = "session_start",
        );
        let self_clone = self.clone();
        let session_id_str = session_id.to_string();
        let denied_flag = Arc::new(Mutex::new(false));
        let denied_flag_clone = denied_flag.clone();

        let join_handle = tokio::spawn(async move {
            self_clone.run_turn_internal(&session_id_str, message, denied_flag_clone, on_token).await
        });

        let abort_handle = join_handle.abort_handle();
        self.active_sessions.lock_unpoison().insert(session_id.to_string(), abort_handle);

        let res = join_handle.await;
        self.active_sessions.lock_unpoison().remove(session_id);

        let elapsed = start_time.elapsed();
        tracing::info!(
            target = "maho_agent::audit",
            session_id = %session_id,
            event = "session_end",
            duration_ms = elapsed.as_millis(),
        );

        match res {
            Ok(result) => result,
            Err(e) => {
                if e.is_cancelled() {
                    Err(AgentError::Cancelled)
                } else {
                    Err(AgentError::ExecutionError(e.to_string()))
                }
            }
        }
    }

    async fn cancel(&self, session_id: &str) -> Result<(), AgentError> {
        let mut active = self.active_sessions.lock_unpoison();
        if let Some(abort_handle) = active.remove(session_id) {
            abort_handle.abort();
            Ok(())
        } else {
            Err(AgentError::SessionNotFound(session_id.to_string()))
        }
    }

    async fn list_tools(&self) -> Result<Vec<ToolDefinition>, AgentError> {
        Ok(crate::tool_registry::get_platform_tools())
    }

    fn set_permission_callback(
        &self,
        callback: Box<dyn Fn(PermissionRequest) -> std::pin::Pin<Box<dyn std::future::Future<Output = PermissionDecision> + Send>> + Send + Sync + 'static>,
    ) -> Result<(), AgentError> {
        let mut lock = self.permission_callback.lock_unpoison();
        *lock = Some(callback);
        Ok(())
    }

    fn set_secure_storage_callback(
        &self,
        callback: Box<dyn Fn(&str) -> Result<ZeroizedString, AgentError> + Send + Sync + 'static>,
    ) -> Result<(), AgentError> {
        let mut lock = self.secure_storage_callback.lock_unpoison();
        *lock = Some(callback);
        Ok(())
    }
}

#[cfg(test)]
#[cfg(not(any(target_os = "ios", target_os = "android")))]
mod tests {
    use super::*;

    #[tokio::test]
    async fn test_fsread_size_cap_rejection() {
        let temp_dir = tempfile::tempdir().unwrap();
        let big_path = temp_dir.path().join("big.txt");
        let big_content = vec![b'X'; (FS_READ_MAX_BYTES as usize) + 1];
        tokio::fs::write(&big_path, &big_content).await.unwrap();

        let tool = FsReadTool {
            workspace_root: temp_dir.path().to_path_buf(),
        };

        let tool_call = ToolCall::builder()
            .id("test-1".to_string())
            .name("fs_read".to_string())
            .args(format!("{{\"path\": \"{}\"}}", big_path.display()))
            .build()
            .unwrap();

        let context = swiftide_agents::DefaultContext::default();
        let result = tool.invoke(&context, &tool_call).await;

        let err = result.expect_err("fs_read should reject files larger than FS_READ_MAX_BYTES");
        let msg = err.to_string();
        assert!(
            msg.contains("File too large"),
            "Error should mention size cap; got: {msg}"
        );
    }

    #[tokio::test]
    async fn test_fsread_within_size_cap_succeeds() {
        let temp_dir = tempfile::tempdir().unwrap();
        let small_path = temp_dir.path().join("small.txt");
        tokio::fs::write(&small_path, b"hello world").await.unwrap();

        let tool = FsReadTool {
            workspace_root: temp_dir.path().to_path_buf(),
        };

        let tool_call = ToolCall::builder()
            .id("test-2".to_string())
            .name("fs_read".to_string())
            .args(format!("{{\"path\": \"{}\"}}", small_path.display()))
            .build()
            .unwrap();

        let context = swiftide_agents::DefaultContext::default();
        let result = tool.invoke(&context, &tool_call).await;

        let output = result.expect("fs_read should succeed for files under the cap");
        let text = match output {
            ToolOutput::Text(t) => t,
            other => panic!("Expected text output; got {other:?}"),
        };
        assert_eq!(text, "hello world");
    }
}
